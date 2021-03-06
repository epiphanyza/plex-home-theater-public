//
//  PlexRemoteSubscriberManager.cpp
//  Plex Home Theater
//
//  Created by Tobias Hieta on 2013-08-19.
//
//

#include "PlexRemoteSubscriberManager.h"
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>

#include "utils/log.h"
#include "video/VideoInfoTag.h"

#include "Application.h"
#include "PlexJobs.h"

#include "Client/PlexMediaServerClient.h"
#include "PlexApplication.h"

////////////////////////////////////////////////////////////////////////////////////////
CPlexRemoteSubscriber::CPlexRemoteSubscriber(const std::string &uuid, const std::string &ipaddress, int port)
{
  m_url.SetProtocol("http");
  m_url.SetHostName(ipaddress);
  m_url.SetPort(port);
  
  m_uuid = uuid;
}

////////////////////////////////////////////////////////////////////////////////////////
bool CPlexRemoteSubscriber::shouldRemove() const
{
  if (m_lastUpdated.elapsed() > PLEX_REMOTE_SUBSCRIBER_REMOVE_INTERVAL)
  {
    CLog::Log(LOGDEBUG, "CPlexRemoteSubscriber::shouldRemove removing %s because elapsed: %d", m_uuid.c_str(), m_lastUpdated.elapsed());
    return true;
  }
  CLog::Log(LOGDEBUG, "CPlexRemoteSubscriber::shouldRemove will not remove because elapsed: %d", m_lastUpdated.elapsed());
  return false;
}

////////////////////////////////////////////////////////////////////////////////////////
void CPlexRemoteSubscriber::refresh()
{
  CLog::Log(LOGDEBUG, "CPlexRemoteSubscriber::refresh %s", m_uuid.c_str());
  m_lastUpdated.restart();
}

////////////////////////////////////////////////////////////////////////////////////////
void CPlexRemoteSubscriberManager::addSubscriber(CPlexRemoteSubscriberPtr subscriber)
{
  if (!subscriber) return;
  
  CSingleLock lk(m_crit);
  
  if (m_map.find(subscriber->getUUID()) != m_map.end())
  {
    CLog::Log(LOGDEBUG, "CPlexRemoteSubscriberManager::addSubscriber refreshed %s", subscriber->getUUID().c_str());
    m_map[subscriber->getUUID()]->refresh();
  }
  else
  {
    m_map[subscriber->getUUID()] = subscriber;
    CLog::Log(LOGDEBUG, "CPlexRemoteSubscriberManager::addSubscriber added %s", subscriber->getUUID().c_str());
    
    CPlexMediaServerClient::MediaState state;
    CFileItemPtr item;
    
    if (g_application.IsPlaying())
    {
      if (g_application.GetPlaySpeed() == 0)
        state = CPlexMediaServerClient::MEDIA_STATE_PAUSED;
      else
        state = CPlexMediaServerClient::MEDIA_STATE_PLAYING;
      
      item = g_application.CurrentFileItemPtr();
    }
    else
      state = CPlexMediaServerClient::MEDIA_STATE_STOPPED;
    
    g_plexApplication.mediaServerClient->ReportItemProgressToSubscriber(subscriber->getURL(), item, state, g_application.GetTime());
  }
  
  if (!m_refreshTimer.IsRunning())
    m_refreshTimer.Start(PLEX_REMOTE_SUBSCRIBER_CHECK_INTERVAL * 1000, true);
}

////////////////////////////////////////////////////////////////////////////////////////
void CPlexRemoteSubscriberManager::removeSubscriber(CPlexRemoteSubscriberPtr subscriber)
{
  if (!subscriber) return;
  
  CSingleLock lk(m_crit);
  
  if (m_map.find(subscriber->getUUID()) == m_map.end())
    return;
  
  m_map.erase(subscriber->getUUID());
  
  if (m_map.size() == 0)
    m_refreshTimer.Stop();
}

////////////////////////////////////////////////////////////////////////////////////////
void CPlexRemoteSubscriberManager::OnTimeout()
{
  CSingleLock lk(m_crit);
  
  std::vector<std::string> subsToRemove;
  
  CLog::Log(LOGDEBUG, "CPlexRemoteSubscriberManager::OnTimeout starting with %lu clients", m_map.size());
  
  BOOST_FOREACH(SubscriberPair p, m_map)
  {
    if (p.second->shouldRemove())
      subsToRemove.push_back(p.first);
  }
  
  BOOST_FOREACH(std::string s, subsToRemove)
    m_map.erase(s);
  
  CLog::Log(LOGDEBUG, "CPlexRemoteSubscriberManager::OnTimeout %lu clients left after timeout", m_map.size());
  
  /* still clients to handle, restart the timer */
  if (m_map.size() < 1)
    m_refreshTimer.Stop();
}

////////////////////////////////////////////////////////////////////////////////////////
std::vector<CURL> CPlexRemoteSubscriberManager::getSubscriberURL() const
{
  std::vector<CURL> list;
  if (!hasSubscribers())
    return list;
  
  CSingleLock lk(m_crit);
  BOOST_FOREACH(SubscriberPair p, m_map)
    list.push_back(p.second->getURL());
  
  return list;
}
