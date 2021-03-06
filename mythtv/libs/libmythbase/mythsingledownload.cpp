
#include "mythsingledownload.h"
#include "mythlogging.h"

/*
 * For simple one-at-a-time downloads, this routine (found at
 * http://www.insanefactory.com/2012/08/qt-http-request-in-a-single-function/
 * ) works well.
 */

bool MythSingleDownload::DownloadURL(const QUrl &url, QByteArray *buffer,
                                     uint timeout, uint redirs)
{
    m_lock.lock();

    // create custom temporary event loop on stack
    QEventLoop   event_loop;

    // the HTTP request
    QNetworkRequest req(url);
    m_replylock.lock();
    m_reply = m_mgr.get(req);
    m_replylock.unlock();

    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::AlwaysNetwork);

    // "quit()" the event-loop, when the network request "finished()"
    connect(m_reply, SIGNAL(finished()), &event_loop, SLOT(quit()));
    connect(&m_timer, SIGNAL(timeout()), &event_loop, SLOT(quit()));

    // Configure timeout
    m_timer.setSingleShot(true);
    m_timer.start(timeout);  // 30 secs. by default

    bool ret = event_loop.exec(); // blocks stack until quit() is called

    disconnect(&m_timer, SIGNAL(timeout()), &event_loop, SLOT(quit()));
    disconnect(m_reply, SIGNAL(finished()), &event_loop, SLOT(quit()));

    if (ret != 0)
    {
        LOG(VB_GENERAL, LOG_ERR, QString("MythSingleDownload evenloop failed"));
    }

    m_replylock.lock();
    if (m_timer.isActive())
    {
        m_timer.stop();
        m_errorcode = m_reply->error();

        QString redir = m_reply->attribute(
            QNetworkRequest::RedirectionTargetAttribute).toUrl().toString();

        if (redir.length())
        {
            if (redirs > 3)
            {
                LOG(VB_GENERAL, LOG_ERR, QString("%1: too many redirects").arg(url.toString()));
                ret = false;
            }
            else
            {
                LOG(VB_GENERAL, LOG_INFO, QString("%1 -> %2").arg(url.toString()).arg(redir));
                m_replylock.unlock();
                m_lock.unlock();
                return DownloadURL(redir, buffer, timeout, redirs + 1);
            }
        }

        if (m_errorcode == QNetworkReply::NoError)
        {
            *buffer += m_reply->readAll();
            delete m_reply;
            m_reply = NULL;
            m_errorstring.clear();
            ret = true;
        }
        else
        {
            m_errorstring = m_reply->errorString();
            delete m_reply;
            m_reply = NULL;
            ret = false;
        }
    }
    else
    {
        m_errorstring = "timed-out";
        m_timer.stop();
        m_reply->abort();
        delete m_reply;
        m_reply = NULL;
        ret = false;
    }
    m_replylock.unlock();
    m_lock.unlock();
    return ret;
}

void MythSingleDownload::Cancel(void)
{
    QMutexLocker  replylock(&m_replylock);
    if (m_reply)
    {
        LOG(VB_GENERAL, LOG_INFO, "MythSingleDownload: Aborting download");
        m_reply->abort();
    }
}
