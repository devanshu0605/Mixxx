
#include <QtDebug>
#include <QtCore>
#include <QMutableMapIterator>
#include <QSqlQuery>
#include <QSqlError>
#include "trackinfoobject.h"
#include "library/dao/trackdao.h"

TrackDAO::TrackDAO(QSqlDatabase& database, CueDAO& cueDao)
        : m_database(database),
          m_cueDao(cueDao) {

}

TrackDAO::~TrackDAO()
{
}

void TrackDAO::initialize()
{
    qDebug() << "TrackDAO::initialize" << QThread::currentThread() << m_database.connectionName();
}

/** Retrieve the track id for the track that's located at "location" on disk.
    @return the track id for the track located at location, or -1 if the track
            is not in the database.
*/
int TrackDAO::getTrackId(QString location)
{
    //qDebug() << "TrackDAO::getTrackId" << QThread::currentThread() << m_database.connectionName();

    QSqlQuery query(m_database);
    query.prepare("SELECT library.id FROM library INNER JOIN track_locations ON library.location WHERE track_locations.location=:location");
    query.bindValue(":location", location);

    if (!query.exec()) {
        qDebug() << query.lastError();
        return -1;
    }

    int libraryTrackId = -1;
    if (query.next()) {
        libraryTrackId = query.value(query.record().indexOf("id")).toInt();
    }
    //query.finish();

    return libraryTrackId;
}

/** Some code (eg. drag and drop) needs to just get a track's location, and it's
    not worth retrieving a whole TrackInfoObject.*/
QString TrackDAO::getTrackLocation(int trackId)
{
    qDebug() << "TrackDAO::getTrackLocation"
             << QThread::currentThread() << m_database.connectionName();
    QSqlQuery query(m_database);
    QString trackLocation = "";
    query.prepare("SELECT track_locations.location FROM track_locations INNER JOIN library ON library.location = track_locations.id WHERE library.id=:id");
    query.bindValue(":id", trackId);
    if (!query.exec()) {
        qDebug() << query.lastError();
        return "";
    }
    while (query.next()) {
        trackLocation = query.value(query.record().indexOf("location")).toString();
    }
    //query.finish();

    return trackLocation;
}

/** Check if a track exists in the library table already.
    @param file_location The full path to the track on disk, including the filename.
    @return true if the track is found in the library table, false otherwise.
*/
bool TrackDAO::trackExistsInDatabase(QString location)
{
    return (getTrackId(location) != -1);
}

void TrackDAO::saveTrack(TrackInfoObject* pTrack) {
    // If track's id is not -1, then update, otherwise add.
    if (pTrack->getId() != -1) {
        if (pTrack->isDirty()) {
            updateTrack(pTrack);
        } else {
            qDebug() << "Skipping track update for track" << pTrack->getId();
        }
    } else {
        addTrack(pTrack);
    }
}

void TrackDAO::saveDirtyTracks() {
    QMapIterator<int, TrackInfoObject*> it(m_tracks);
    while (it.hasNext()) {
        it.next();
        TrackInfoObject* pTrack = it.value();
        if (pTrack && pTrack->isDirty()) {
            saveTrack(pTrack);
        }
    }
}

int TrackDAO::addTrack(QString location)
{
    QFileInfo file(location);
    int trackId = -1;
    TrackInfoObject * pTrack = new TrackInfoObject(file.absoluteFilePath());
    if (pTrack) {
        //Add the song to the database.
        addTrack(pTrack);
        trackId = pTrack->getId();
        delete pTrack;
    }
    return trackId;
}

void TrackDAO::addTrack(TrackInfoObject * pTrack)
{
    //qDebug() << "TrackDAO::addTrack" << QThread::currentThread() << m_database.connectionName();
 	//qDebug() << "TrackCollection::addTrack(), inserting into DB";
    Q_ASSERT(pTrack); //Why you be giving me NULL pTracks

    //Start the transaction
    m_database.transaction();

    QSqlQuery query(m_database);
    int trackLocationId = -1;

    //Insert the track location into the corresponding table. This will fail silently
    //if the location is already in the table because it has a UNIQUE constraint.
    query.prepare("INSERT INTO track_locations (location, directory, filename, filesize, fs_deleted, needs_verification) "
                  "VALUES (:location, :directory, :filename, :filesize, :fs_deleted, :needs_verification)");
    query.bindValue(":location", pTrack->getLocation());
    query.bindValue(":directory", QFileInfo(pTrack->getLocation()).path());
    query.bindValue(":filename", pTrack->getFilename());
    query.bindValue(":filesize", pTrack->getLength());
    query.bindValue(":fs_deleted", 0);
    query.bindValue(":needs_verification", 0);

    if (!query.exec()) {
        // Inserting into track_locations failed, so the file already
        // exists. Query for its id.
        query.prepare("SELECT id FROM track_locations WHERE location=:location");
        query.bindValue(":location", pTrack->getLocation());

        if (!query.exec()) {
            // We can't even select this, something is wrong.
            qDebug() << query.lastError();
            m_database.rollback();
            return;
        }
        while (query.next()) {
            trackLocationId = query.value(query.record().indexOf("id")).toInt();
        }
    } else {
        // Inserting succeeded, so just get the last rowid.
        QVariant lastInsert = query.lastInsertId();
        trackLocationId = lastInsert.toInt();
    }

    //Failure of this assert indicates that we were unable to insert the track
    //location into the table AND we could not retrieve the id of that track
    //location from the same table. "It shouldn't happen"... unless I screwed up
    //- Albert :)
    Q_ASSERT(trackLocationId >= 0);

    query.prepare("INSERT INTO library (artist, title, album, year, genre, tracknumber, "
                  "location, comment, url, duration, "
                  "bitrate, samplerate, cuepoint, bpm, wavesummaryhex, "
                  "channels, mixxx_deleted, header_parsed) "
                  "VALUES (:artist, "
                  ":title, :album, :year, :genre, :tracknumber, "
                  ":location, :comment, :url, :duration, "
                  ":bitrate, :samplerate, :cuepoint, :bpm, :wavesummaryhex, "
                  ":channels, :mixxx_deleted, :header_parsed)");
    query.bindValue(":artist", pTrack->getArtist());
    query.bindValue(":title", pTrack->getTitle());
    query.bindValue(":album", pTrack->getAlbum());
    query.bindValue(":year", pTrack->getYear());
    query.bindValue(":genre", pTrack->getGenre());
    query.bindValue(":tracknumber", pTrack->getTrackNumber());
    query.bindValue(":location", trackLocationId);
    query.bindValue(":comment", pTrack->getComment());
    query.bindValue(":url", pTrack->getURL());
    query.bindValue(":duration", pTrack->getDuration());
    query.bindValue(":bitrate", pTrack->getBitrate());
    query.bindValue(":samplerate", pTrack->getSampleRate());
    query.bindValue(":cuepoint", pTrack->getCuePoint());
    query.bindValue(":bpm", pTrack->getBpm());
    const QByteArray* pWaveSummary = pTrack->getWaveSummary();
    if (pWaveSummary) //Avoid null pointer deref
        query.bindValue(":wavesummaryhex", *pWaveSummary);
    //query.bindValue(":timesplayed", pTrack->getCuePoint());
    //query.bindValue(":datetime_added", pTrack->getDateAdded());
    query.bindValue(":channels", pTrack->getChannels());
    query.bindValue(":mixxx_deleted", 0);
    query.bindValue(":header_parsed", pTrack->getHeaderParsed() ? 1 : 0);

    int trackId = -1;

    if (!query.exec())
    {
        qDebug() << "Failed to INSERT new track into library"
                 << __FILE__ << __LINE__ << query.lastError();
        m_database.rollback();
        return;
    } else {
        // Inserting succeeded, so just get the last rowid.
        trackId = query.lastInsertId().toInt();
    }
    //query.finish();

    Q_ASSERT(trackId >= 0);

    if (trackId >= 0) {
        m_cueDao.saveTrackCues(trackId, pTrack);
    } else {
        qDebug() << "Could not get track ID to save the track cue points:"
                 << query.lastError();
    }

    pTrack->setId(trackId);
    pTrack->setDirty(false);

    //If addTrack() is called on a track that already exists in the library but
    //has been "removed" (ie. mixxx_deleted is 1), then the above INSERT will
    //fail silently. What we really want to do is just mark the track as
    //undeleted, by setting mixxx_deleted to 0.  addTrack() will not get called
    //on files that are already in the library during a rescan (even if
    //mixxx_deleted=1). However, this function WILL get called when a track is
    //dragged and dropped onto the library or when manually imported from the
    //File... menu.  This allows people to re-add tracks that they "removed"...
    query.prepare("UPDATE library "
                  "SET mixxx_deleted=0 "
                  "WHERE id = " + QString("%1").arg(trackId));
    if (!query.exec()) {
        qDebug() << "Failed to set track" << trackId << "as undeleted" << query.lastError();
    }
    //query.finish();

    //Commit the transaction
    m_database.commit();
}

  /** Removes a track from the library track collection. */
void TrackDAO::removeTrack(int id)
{
    qDebug() << "TrackDAO::removeTrack" << QThread::currentThread() << m_database.connectionName();
    Q_ASSERT(id >= 0);
    QSqlQuery query(m_database);

    //Mark the track as deleted!
    query.prepare("UPDATE library "
                  "SET mixxx_deleted=1 "
                  "WHERE id==" + QString("%1").arg(id));
    query.exec();
    //query.finish();

    //Print out any SQL error, if there was one.
    if (query.lastError().isValid()) {
        qDebug() << query.lastError();
    }
}

TrackInfoObject *TrackDAO::getTrackFromDB(QSqlQuery &query) const
{
    TrackInfoObject* track = NULL;
    if (!query.isValid()) {
        //query.exec();
    }

    //Print out any SQL error, if there was one.
    if (query.lastError().isValid()) {
        qDebug() << query.lastError();
    }

    int locationId = -1;
    while (query.next()) {
        track = new TrackInfoObject();
        int trackId = query.value(query.record().indexOf("id")).toInt();

        QString artist = query.value(query.record().indexOf("artist")).toString();
        QString title = query.value(query.record().indexOf("title")).toString();
        QString album = query.value(query.record().indexOf("album")).toString();
        QString year = query.value(query.record().indexOf("year")).toString();
        QString genre = query.value(query.record().indexOf("genre")).toString();
        QString tracknumber = query.value(query.record().indexOf("tracknumber")).toString();
        QString comment = query.value(query.record().indexOf("comment")).toString();
        QString url = query.value(query.record().indexOf("url")).toString();
        int duration = query.value(query.record().indexOf("duration")).toInt();
        int bitrate = query.value(query.record().indexOf("bitrate")).toInt();
        int samplerate = query.value(query.record().indexOf("samplerate")).toInt();
        int cuepoint = query.value(query.record().indexOf("cuepoint")).toInt();
        QString bpm = query.value(query.record().indexOf("bpm")).toString();
        QByteArray* wavesummaryhex = new QByteArray(
            query.value(query.record().indexOf("wavesummaryhex")).toByteArray());
        //int timesplayed = query.value(query.record().indexOf("timesplayed")).toInt();
        int channels = query.value(query.record().indexOf("channels")).toInt();
        int filesize = query.value(query.record().indexOf("filesize")).toInt();
        QString location = query.value(query.record().indexOf("location")).toString();
        bool header_parsed = query.value(query.record().indexOf("header_parsed")).toBool();

        track->setId(trackId);
        track->setArtist(artist);
        track->setTitle(title);
        track->setAlbum(album);
        track->setYear(year);
        track->setGenre(genre);
        track->setTrackNumber(tracknumber);

        track->setComment(comment);
        track->setURL(url);
        track->setDuration(duration);
        track->setBitrate(bitrate);
        track->setSampleRate(samplerate);
        track->setCuePoint((float)cuepoint);
        track->setBpm(bpm.toFloat());
        track->setWaveSummary(wavesummaryhex, false);
        delete wavesummaryhex;
        //track->setTimesPlayed //Doesn't exist wtfbbq
        track->setChannels(channels);
        track->setLocation(location);
        track->setLength(filesize);
        track->setHeaderParsed(header_parsed);

        track->setCuePoints(m_cueDao.getCuesForTrack(trackId));
        track->setDirty(false);

        m_tracks[trackId] = track;

    }
    //query.finish();

    return track;
}

TrackInfoObject *TrackDAO::getTrack(int id) const
{
    qDebug() << "TrackDAO::getTrack" << QThread::currentThread() << m_database.connectionName();

    if (m_tracks.contains(id)) {
        qDebug() << "Returning cached TIO for track" << id;
        return m_tracks[id];
    }

    QSqlQuery query(m_database);

    query.prepare("SELECT library.id, artist, title, album, year, genre, tracknumber, track_locations.location as location, track_locations.filesize as filesize, comment, url, duration, bitrate, samplerate, cuepoint, bpm, wavesummaryhex, channels, header_parsed FROM Library INNER JOIN track_locations ON library.location = track_locations.id WHERE library.id=" + QString("%1").arg(id));
    TrackInfoObject* track = NULL;
    if (query.exec()) {
         track = getTrackFromDB(query);
    } else {
        qDebug() << QString("getTrack(%1)").arg(id) << query.lastError();
    }

    return track;
}

/** Saves a track's info back to the database */
void TrackDAO::updateTrack(TrackInfoObject* pTrack)
{
    Q_ASSERT(pTrack);
    qDebug() << "TrackDAO::updateTrackInDatabase" << QThread::currentThread() << m_database.connectionName();

    qDebug() << "Updating track" << pTrack->getInfo() << "in database...";

    int trackId = pTrack->getId();
    Q_ASSERT(trackId >= 0);

    QSqlQuery query(m_database);

    //Update everything but "location", since that's what we identify the track by.
    query.prepare("UPDATE library "
                  "SET artist=:artist, "
                  "title=:title, album=:album, year=:year, genre=:genre, "
                  "tracknumber=:tracknumber, "
                  "comment=:comment, url=:url, duration=:duration, "
                  "bitrate=:bitrate, samplerate=:samplerate, cuepoint=:cuepoint, "
                  "bpm=:bpm, wavesummaryhex=:wavesummaryhex, "
                  "channels=:channels, header_parsed=:header_parsed "
                  "WHERE id="+QString("%1").arg(trackId));
    query.bindValue(":artist", pTrack->getArtist());
    query.bindValue(":title", pTrack->getTitle());
    query.bindValue(":album", pTrack->getAlbum());
    query.bindValue(":year", pTrack->getYear());
    query.bindValue(":genre", pTrack->getGenre());
    query.bindValue(":tracknumber", pTrack->getTrackNumber());
    query.bindValue(":comment", pTrack->getComment());
    query.bindValue(":url", pTrack->getURL());
    query.bindValue(":duration", pTrack->getDuration());
    query.bindValue(":bitrate", pTrack->getBitrate());
    query.bindValue(":samplerate", pTrack->getSampleRate());
    query.bindValue(":cuepoint", pTrack->getCuePoint());
    query.bindValue(":bpm", pTrack->getBpm());
    const QByteArray* pWaveSummary = pTrack->getWaveSummary();
    if (pWaveSummary) //Avoid null pointer deref
        query.bindValue(":wavesummaryhex", *pWaveSummary);
    //query.bindValue(":timesplayed", pTrack->getCuePoint());
    query.bindValue(":channels", pTrack->getChannels());
    query.bindValue(":header_parsed", pTrack->getHeaderParsed() ? 1 : 0);
    //query.bindValue(":location", pTrack->getLocation());

    if (!query.exec()) {
        qDebug() << query.lastError();
        return;
    }
    //query.finish();

    m_cueDao.saveTrackCues(trackId, pTrack);

    pTrack->setDirty(false);
}


void TrackDAO::invalidateTrackLocations(QString directory)
{
    //qDebug() << "TrackDAO::invalidateTrackLocations" << QThread::currentThread() << m_database.connectionName();
    //qDebug() << "invalidateTrackLocations(" << directory << ")";

    QSqlQuery query(m_database);
    query.prepare("UPDATE track_locations "
                  "SET needs_verification=1 "
                  "WHERE directory=:directory");
    query.bindValue(":directory", directory);
    if (!query.exec()) {
        qDebug() << "Couldn't mark tracks in directory" << directory <<  "as needing verification." << query.lastError();
    }
}

void TrackDAO::markTrackLocationAsVerified(QString location)
{
    //qDebug() << "TrackDAO::markTrackLocationAsVerified" << QThread::currentThread() << m_database.connectionName();
    //qDebug() << "markTrackLocationAsVerified()" << location;

    QSqlQuery query(m_database);
    query.prepare("UPDATE track_locations "
                  "SET needs_verification=0, fs_deleted=0 "
                  "WHERE location=:location");
    query.bindValue(":location", location);
    if (!query.exec()) {
        qDebug() << "Couldn't mark track" << location << " as verified." << query.lastError();
    }

}

void TrackDAO::markUnverifiedTracksAsDeleted()
{
    qDebug() << "TrackDAO::markUnverifiedTracksAsDeleted" << QThread::currentThread() << m_database.connectionName();
    //qDebug() << "markUnverifiedTracksAsDeleted()";

    QSqlQuery query(m_database);
    query.prepare("UPDATE track_locations "
                  "SET fs_deleted=1 "
                  "WHERE needs_verification=1");
    if (!query.exec()) {
        qDebug() << "Couldn't mark unverified tracks as deleted." << query.lastError();
    }

}

void TrackDAO::markTrackLocationsAsDeleted(QString directory)
{
    //qDebug() << "TrackDAO::markTrackLocationsAsDeleted" << QThread::currentThread() << m_database.connectionName();
    QSqlQuery query(m_database);
    query.prepare("UPDATE track_locations "
                  "SET fs_deleted=1 "
                  "WHERE directory=:directory");
    query.bindValue(":directory", directory);
    if (!query.exec()) {
        qDebug() << "Couldn't mark tracks in" << directory << "as deleted." << query.lastError();
    }
}

/** Look for moved files. Look for files that have been marked as "deleted on disk"
    and see if another "file" with the same name and filesize exists in the track_locations
    table. That means the file has moved instead of being deleted outright, and so
    we can salvage your existing metadata that you have in your DB (like cue points, etc.). */
void TrackDAO::detectMovedFiles()
{
    //qDebug() << "markUnverifiedTracksAsDeleted()";
    m_database.transaction();


    QSqlQuery query(m_database);
    QSqlQuery query2(m_database);
    int oldTrackLocationId = -1;
    int newTrackLocationId = -1;
    QString filename;
    int fileSize;

    query.prepare("SELECT id, filename, filesize FROM track_locations WHERE fs_deleted=1");
    query.exec();

    //For each track that's been "deleted" on disk...
    while (query.next()) {
        newTrackLocationId = -1; //Reset this var
        oldTrackLocationId = query.value(query.record().indexOf("id")).toInt();
        filename = query.value(query.record().indexOf("filename")).toString();
        fileSize = query.value(query.record().indexOf("filesize")).toInt();

        query2.prepare("SELECT id FROM track_locations WHERE "
                       "fs_deleted=0 AND "
                       "filename=:filename AND "
                       "filesize=:filesize");
        query2.bindValue(":filename", filename);
        query2.bindValue(":filesize", fileSize);
        Q_ASSERT(query2.exec());

        Q_ASSERT(query2.size() <= 1); //WTF duplicate tracks?
        while (query2.next())
        {
            newTrackLocationId = query2.value(query2.record().indexOf("id")).toInt();
        }

        //If we found a moved track...
        if (newTrackLocationId >= 0)
        {
            qDebug() << "Found moved track!" << filename;

            //Remove old row from track_locations table
            query2.prepare("DELETE FROM track_locations WHERE "
                           "id=:id");
            query2.bindValue(":id", oldTrackLocationId);
            Q_ASSERT(query2.exec());

            //The library scanner will have added a new row to the Library
            //table which corresponds to the track in the new location. We need
            //to remove that so we don't end up with two rows in the library table
            //for the same track.
            query2.prepare("DELETE FROM library WHERE "
                           "location=:location");
            query2.bindValue(":location", newTrackLocationId);
            Q_ASSERT(query2.exec());

            //Update the location foreign key for the existing row in the library table
            //to point to the correct row in the track_locations table.
            query2.prepare("UPDATE library "
                           "SET location=:newloc WHERE location=:oldloc");
            query2.bindValue(":newloc", newTrackLocationId);
            query2.bindValue(":oldloc", oldTrackLocationId);
            Q_ASSERT(query2.exec());
        }
    }

    m_database.commit();
}
