#include "channelmanager.h"
#include <QStringRef>
#include <QDir>
#include <QProcess>
#include "../util/fileutils.h"
#include <QThread>
#include <QJsonArray>
#include <QApplication>
#include <QStandardPaths>


ChannelManager::ChannelManager(NetworkManager *netman) : netman(netman){
    alert = true;
    closeToTray = false;

    access_token = "";
    tempFavourites = 0;

    alertPosition = 1;

    favouritesModel = new ChannelListModel();

    resultsModel = new ChannelListModel();

    featuredModel = new ChannelListModel();

    gamesModel = new GameListModel();

    connect(favouritesModel,SIGNAL(channelOnlineStateChanged(Channel*)),this,SLOT(notify(Channel*)));

    favouritesProxy = new QSortFilterProxyModel();
    favouritesProxy->setSourceModel(favouritesModel);
    favouritesProxy->setSortRole(ChannelListModel::Roles::ViewersRole);
    favouritesProxy->sort(0, Qt::DescendingOrder);

    featuredProxy = new QSortFilterProxyModel();
    featuredProxy->setSourceModel(featuredModel);
    featuredProxy->setSortRole(ChannelListModel::Roles::ViewersRole);
    featuredProxy->sort(0, Qt::DescendingOrder);

    connect(netman, SIGNAL(allStreamsOperationFinished(const QList<Channel*>&)), this, SLOT(updateStreams(QList<Channel*>)));
    connect(netman, SIGNAL(featuredStreamsOperationFinished(const QList<Channel*>&)), this, SLOT(addFeaturedResults(QList<Channel*>)));
    connect(netman, SIGNAL(gamesOperationFinished(const QList<Game*>&)), this, SLOT(addGames(QList<Game*>)));
    connect(netman, SIGNAL(gameStreamsOperationFinished(const QList<Channel*>&)), this, SLOT(addSearchResults(QList<Channel*>)));
    connect(netman, SIGNAL(searchChannelsOperationFinished(const QList<Channel*>&)), this, SLOT(addSearchResults(QList<Channel*>)));
    connect(netman, SIGNAL(m3u8OperationFinished(const QStringList&)), this, SLOT(onFoundPlaybackStream(const QStringList&)));
    connect(netman, SIGNAL(searchGamesOperationFinished(QList<Game*>)), this, SLOT(addGames(QList<Game*>)));

    connect(netman, SIGNAL(userNameOperationFinished(QString)), this, SLOT(onUserNameUpdated(QString)));
    connect(netman, SIGNAL(favouritesReplyFinished(const QList<Channel*>&, const quint32)), this, SLOT(addFollowedResults(const QList<Channel*>&, const quint32)));

    connect(netman, SIGNAL(networkAccessChanged(bool)), this, SLOT(onNetworkAccessChanged(bool)));
}

ChannelManager::~ChannelManager(){
    qDebug() << "Destroyer: ChannelManager";

    save();

    delete favouritesModel;
    delete resultsModel;
    delete featuredModel;
    delete gamesModel;
    delete favouritesProxy;
    delete featuredProxy;
}

QString appPath(){
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    if (!dir.mkpath("./orion")) {
        qDebug() << "Error creating data dir!";
    }
    dir.cd("./orion");

    return dir.absoluteFilePath(DATA_FILE);
}

QSortFilterProxyModel *ChannelManager::getFeaturedProxy() const
{
    return featuredProxy;
}

bool ChannelManager::isAlert() const
{
    return alert;
}

int ChannelManager::getAlertPosition() const
{
    return alertPosition;
}

void ChannelManager::setAlertPosition(const int &value)
{
    alertPosition = value;
}

void ChannelManager::addToFavourites(const quint32 &id, const QString &serviceName, const QString &title,
                                     const QString &info, const QString &logo, const QString &preview,
                                     const QString &game, const qint32 &viewers, bool online)
{
    if (!favouritesModel->find(id)){
        Channel *channel = new Channel();
        channel->setId(id);
        channel->setServiceName(serviceName);
        channel->setName(title);
        channel->setInfo(info);
        channel->setLogourl(logo);
        channel->setPreviewurl(preview);
        channel->setGame(game);
        channel->setOnline(online);
        channel->setViewers(viewers);

        if (isAccessTokenAvailable() && !user_name.isEmpty()) {
            netman->editUserFavourite(access_token, user_name, channel->getServiceName(), true);
        }

        favouritesModel->addChannel(channel);

        emit addedChannel(channel->getId());

        Channel *chan = resultsModel->find(channel->getId());
        if (chan){
            chan->setFavourite(true);
            resultsModel->updateChannelForView(chan);
        }

        //Update featured also
        chan = featuredModel->find(channel->getId());
        if (chan){
            chan->setFavourite(true);
            featuredModel->updateChannelForView(chan);
        }
    }
}

bool ChannelManager::isCloseToTray() const
{
    return closeToTray;
}

void ChannelManager::setCloseToTray(bool arg)
{
    closeToTray = arg;
}

void ChannelManager::searchGames(QString q, const quint32 &offset, const quint32 &limit)
{
    if (offset == 0 || !q.isEmpty())
        gamesModel->clear();

    //If query is empty, search games by viewercount
    if (q.isEmpty())
        netman->getGames(offset, limit);

    //Else by queryword
    else if (offset == 0)
        netman->searchGames(q);

    emit gamesSearchStarted();
}

QString ChannelManager::username() const
{
    return user_name;
}

QString ChannelManager::accessToken() const
{
    return access_token;
}

void ChannelManager::setAccessToken(const QString &arg)
{
    access_token = arg;

    if (isAccessTokenAvailable()) {
        //Fetch display name for logged in user
        netman->getUser(access_token);

        //move favs to tempfavs
        if (!tempFavourites) {
            tempFavourites = favouritesModel;

            favouritesModel = new ChannelListModel();
            connect(favouritesModel,SIGNAL(channelOnlineStateChanged(Channel*)),this,SLOT(notify(Channel*)));
            favouritesProxy->setSourceModel(favouritesModel);
        }
    }

    else {
        //Reload local favourites from memory
        if (tempFavourites) {
            delete favouritesModel;
            favouritesModel = tempFavourites;
            tempFavourites = 0;
            favouritesProxy->setSourceModel(favouritesModel);
        }
    }

    emit accessTokenUpdated();
}

ChannelListModel *ChannelManager::getFavouritesModel() const
{
    return favouritesModel;
}

QSortFilterProxyModel *ChannelManager::getFavouritesProxy() const
{
    return favouritesProxy;
}

GameListModel *ChannelManager::getGamesModel() const
{
    return gamesModel;
}

ChannelListModel *ChannelManager::getResultsModel() const
{
    return resultsModel;
}

void ChannelManager::checkResources()
{
    if (!QFile::exists(appPath())){
        QFile file(appPath());
        file.open(QIODevice::ReadWrite);
        file.write("{}");
        qDebug() << "Created data file!";
    }
}



bool ChannelManager::load(){

    qDebug() << "Opening JSON...";

    QJsonParseError error;
    if (!QFile::exists(appPath())){
        qDebug() << "File doesn't exist";
        return false;
    }
    QJsonObject json = QJsonDocument::fromJson(util::readFile(appPath()).toUtf8(),&error).object();

    if (error.error != QJsonParseError::NoError){
        qDebug() << "Parsing error!";
        return false;
    }

    if (!json["alert"].isNull()){
        alert = json["alert"].toBool();
    }

    if (!json["alertPosition"].isNull()){
        alertPosition = json["alertPosition"].toInt();
    }

    if (!json["closeToTray"].isNull()){
        closeToTray = json["closeToTray"].toBool();
    }

    if (json["channels"].isUndefined()){
        qDebug() << "Error: Bad file format: Missing field \"channels\"";
        return false;
	}

    if (!json["channels"].isArray()){
        qDebug() << "Error: Bad file format: channels is not array";
        return false;
    }

    const QJsonArray &arr = json["channels"].toArray();

    QList<Channel*> _channels;

    foreach(const QJsonValue &value, arr){
        QJsonObject obj = value.toObject();

        if (obj["title"].isUndefined() || obj["title"].isNull()){
            qDebug() << "title is missing";
        }
        if (obj["uri"].isUndefined() || obj["uri"].isNull()){
            qDebug() << "uri is missing";
        }
        if (obj["info"].isUndefined() || obj["info"].isNull()){
            qDebug() << "info is missing";
        }
        if (obj["alert"].isUndefined() || obj["alert"].isNull()){
            qDebug() << "alert is missing";
        }
        if (obj["lastSeen"].isUndefined() || obj["lastSeen"].isNull()){
            qDebug() << "lastSeen is missing";
        }
        if (obj["logo"].isUndefined() && obj["logo"].isNull()){
            qDebug() << "logo is missing";
        }
        if (obj["preview"].isUndefined() && obj["preview"].isNull()){
            qDebug() << "preview is missing";
        }

        Channel* channel = new Channel(
                    obj["uri"].toString(),
                    obj["title"].toString(),
                    obj["info"].toString(),
                    obj["alert"].toBool(),
                    obj["lastSeen"].toInt(),
                    obj["logo"].toString(),
                    obj["preview"].toString());
        channel->setId(obj["id"].toInt());

        _channels.append(channel);
    }

    favouritesModel->addAll(_channels);

    qDeleteAll(_channels);

    if (!json["access_token"].isNull()){
        setAccessToken(json["access_token"].toString());
    }

	return true;
}

bool ChannelManager::save()
{
    QJsonArray arr;

    if (tempFavourites) {
        delete favouritesModel;
        favouritesModel = tempFavourites;
        tempFavourites = 0;
    }

    foreach (Channel* channel, favouritesModel->getChannels()){
        arr.append(QJsonValue(channel->getJSON()));
    }

    QJsonValue val(arr);
    QJsonObject obj;
    obj["channels"] = val;
    obj["alert"] = QJsonValue(alert);
    obj["alertPosition"] = QJsonValue(alertPosition);
    obj["closeToTray"] = QJsonValue(closeToTray);
    obj["access_token"] = QJsonValue(access_token);

    return util::writeFile(appPath(),QJsonDocument(obj).toJson());
}

void ChannelManager::addToFavourites(const quint32 &id){
    Channel *channel = resultsModel->find(id);

    if (!channel){
        channel = featuredModel->find(id);
    }

    if (channel){

        if (isAccessTokenAvailable() && !user_name.isEmpty()) {
            netman->editUserFavourite(access_token, user_name, channel->getServiceName(), true);
        }

        favouritesModel->addChannel(new Channel(*channel));

        channel->setFavourite(true);
        emit addedChannel(channel->getId());

        resultsModel->updateChannelForView(channel);

        //Update featured also
        featuredModel->updateChannelForView(channel);

        if (!isAccessTokenAvailable())
            save();
    }
}

Channel* ChannelManager::findFavourite(const QString &q){
    return favouritesModel->find(q);
}

void ChannelManager::removeFromFavourites(const quint32 &id){
    Channel *chan = favouritesModel->find(id);

    emit deletedChannel(chan->getId());

    if (isAccessTokenAvailable() && !user_name.isEmpty()) {
        netman->editUserFavourite(access_token, user_name, chan->getServiceName(), false);
    }

    favouritesModel->removeChannel(chan);

    chan = 0;

    //Update results
    Channel* channel = resultsModel->find(id);
    if (channel){

        channel->setFavourite(false);
        resultsModel->updateChannelForView(channel);
    }

    //Update featured
    channel = featuredModel->find(id);
    if (channel){
        channel->setFavourite(false);
        featuredModel->updateChannelForView(channel);
    }

    if (!isAccessTokenAvailable())
        save();
}

void ChannelManager::play(const QString &url){
    if (QFile::exists(PLAY_FILE)){

        if (!QFileInfo(PLAY_FILE).isExecutable())
            QProcess::execute("chmod +x " + QString(PLAY_FILE));

        QStringList args;
        args << url;

        QProcess::startDetached(PLAY_FILE, args);
    }
    else qDebug() << "Couldn't locate 'play.sh'";
}

void ChannelManager::checkStreams(const QList<Channel *> &list)
{
    if (list.size() == 0)
        return;

    int c_index = 0;
    QString channelsUrl = "";

    foreach(Channel* channel, list){
        if (c_index++ > 0)
            channelsUrl += ",";
        channelsUrl += channel->getServiceName();

        if (c_index >= list.size() || c_index >= 50){
            QString url = KRAKEN_API
                    + QString("/streams?limit=%1&channel=").arg(c_index)
                    + channelsUrl;

            netman->getStreams(url);

            channelsUrl = "";
            c_index = 0;
        }
    }
}

void ChannelManager::checkFavourites()
{
    checkStreams(favouritesModel->getChannels());
}

void ChannelManager::searchChannels(QString q, const quint32 &offset, const quint32 &limit, bool clear)
{
    if (clear)
        resultsModel->clear();

    if (q.startsWith(":game ")){
        q.replace(":game ", "");
        netman->getStreamsForGame(q, offset, limit);

    } else {
        netman->searchChannels(q, offset, limit);
    }

    emit searchingStarted();
}

void ChannelManager::addSearchResults(const QList<Channel*> &list)
{
    bool needsStreamCheck = false;

    foreach (Channel *channel, list){
        if (favouritesModel->find(channel->getId()))
            channel->setFavourite(true);

        if (!channel->isOnline())
            needsStreamCheck = true;
    }

    resultsModel->addAll(list);

    if (needsStreamCheck)
        checkStreams(list);

    qDeleteAll(list);

    emit resultsUpdated();
}

void ChannelManager::getFeatured()
{
    featuredModel->clear();

    netman->getFeaturedStreams();
}

void ChannelManager::findPlaybackStream(const QString &serviceName)
{
    netman->getChannelPlaybackStream(serviceName);
}

void ChannelManager::setAlert(const bool &val)
{
    alert = val;
}

void ChannelManager::onFoundPlaybackStream(const QStringList &items)
{
    emit foundPlaybackStream(items);
}

void ChannelManager::addFeaturedResults(const QList<Channel *> &list)
{
    foreach (Channel *channel, list){
        if (favouritesModel->find(channel->getId()))
            channel->setFavourite(true);
    }

    featuredModel->addAll(list);

    qDeleteAll(list);

    emit featuredUpdated();
}

void ChannelManager::updateFavourites(const QList<Channel*> &list)
{
    favouritesModel->updateChannels(list);
    qDeleteAll(list);
}

bool ChannelManager::containsFavourite(const quint32 &q)
{
    return favouritesModel->find(q) > 0;
}

//Updates channel streams in all models
void ChannelManager::updateStreams(const QList<Channel*> &list)
{
    favouritesModel->updateStreams(list);
    featuredModel->updateStreams(list);
    resultsModel->updateStreams(list);
    qDeleteAll(list);
}

void ChannelManager::addGames(const QList<Game*> &list)
{
    gamesModel->addAll(list);

    qDeleteAll(list);

    emit gamesUpdated();
}

void ChannelManager::notify(Channel *channel)
{
    if (alert && channel){
        emit pushNotification(channel->getName() + (channel->isOnline() ? " is now streaming" : " has gone offline"),
                              channel->getInfo(),
                              channel->getLogourl());
    }
}


//Login function
void ChannelManager::onUserNameUpdated(const QString &name)
{
    user_name = name;
    emit userNameUpdated(user_name);

    if (isAccessTokenAvailable()) {
        //Start using user followed channels
        getFollowedChannels(FOLLOWED_FETCH_LIMIT, 0);
    }
}

void ChannelManager::getFollowedChannels(const quint32& limit, const quint32& offset)
{
    //if (offset == 0)
        //favouritesModel->clear();

    netman->getUserFavourites(user_name, offset, limit);
}


void ChannelManager::addFollowedResults(const QList<Channel *> &list, const quint32 offset)
{
//    qDebug() << "Merging channel data for " << list.size()
//             << " items with " << offset << " offset.";

    favouritesModel->mergeAll(list);

    if (list.size() == FOLLOWED_FETCH_LIMIT)
        getFollowedChannels(FOLLOWED_FETCH_LIMIT, offset);

    qDeleteAll(list);

    emit followedUpdated();

    checkFavourites();
}

void ChannelManager::onNetworkAccessChanged(bool up)
{
    if (up) {
        if (isAccessTokenAvailable()) {
            //Relogin
            favouritesModel->clear();
            netman->getUser(access_token);
        }
    } else {
        qDebug() << "Network went down";
        favouritesModel->setAllChannelsOffline();
        resultsModel->setAllChannelsOffline();
        featuredModel->setAllChannelsOffline();
    }
}
