// RPC stubs for clients to talk to extent_server

#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <ctime>

using namespace std;


// The calls assume that the caller holds a lock on the extent

extent_client::extent_client(std::string dst)
{
  sockaddr_in dstsock;
	make_sockaddr(dst.c_str(), &dstsock);
  cl = new rpcc(dstsock);
  if (cl->bind() != 0) {
    printf("extent_client: bind failed\n");
  }
  assert(pthread_mutex_init(&mutex, NULL) == 0);
}

extent_protocol::status
extent_client::get(extent_protocol::extentid_t eid, std::string &buf)
{
  extent_protocol::status ret = extent_protocol::OK;

  map<extent_protocol::extentid_t, cached_extent>::iterator it;

  //assert(pthread_mutex_lock(&mutex) == 0);

  it = map_client_cache.find(eid);

  // if the extent is cached, read that copy
  if(it != map_client_cache.end()) {
    // if the extent was previously removed
    if(it->second.to_remove)
      ret = extent_protocol::NOENT;
    else {
      buf = it->second.data;
      it->second.attr.atime = std::time(0);
    }
  }
  else {
    ret = cl->call(extent_protocol::get, eid, buf);
    cout << "\n \n DATA in get : " << buf << "thread: " << pthread_self()<< endl;

    if(ret == extent_protocol::OK) {
      struct cached_extent c_ext;
      c_ext.data = buf;
      c_ext.attr.atime = std::time(0);

      map_client_cache[eid] = c_ext;
    }
  }

  //assert(pthread_mutex_unlock(&mutex) == 0);
  return ret;
}

extent_protocol::status
extent_client::getattr(extent_protocol::extentid_t eid, 
		       extent_protocol::attr &attr)
{
  std::cout << "\n IN GET ATTTTTTTTTR"<< "thread: " << pthread_self() << endl;
  extent_protocol::status ret = extent_protocol::OK;

  map<extent_protocol::extentid_t, cached_extent>::iterator it;
  //assert(pthread_mutex_lock(&mutex) == 0);
  it = map_client_cache.find(eid);

  // if the extent is cached, read that copy
  if(it != map_client_cache.end()) {
    if(it->second.to_remove)
      ret = extent_protocol::NOENT;
    else
      attr = it->second.attr;
  }
  else {
    ret = cl->call(extent_protocol::getattr, eid, attr);
  }

  //assert(pthread_mutex_unlock(&mutex) == 0);
  return ret;
}

extent_protocol::status
extent_client::put(extent_protocol::extentid_t eid, std::string buf)
{
  struct cached_extent c_ext;
  c_ext.data = buf;
  c_ext.dirty = true;
  c_ext.attr.size = buf.size();
  c_ext.attr.atime = std::time(0);
  c_ext.attr.mtime = std::time(0);
  c_ext.attr.ctime = std::time(0);

  //assert(pthread_mutex_lock(&mutex) == 0);

  map_client_cache[eid] = c_ext;
  cout << "\n \n DATA in put: " << buf << endl;

  //assert(pthread_mutex_unlock(&mutex) == 0);

  return extent_protocol::OK;
}

extent_protocol::status
extent_client::remove(extent_protocol::extentid_t eid)
{
  struct cached_extent c_ext;
  c_ext.to_remove = true;
  c_ext.dirty = true;

  //assert(pthread_mutex_lock(&mutex) == 0);

  map_client_cache[eid] = c_ext;

  //assert(pthread_mutex_unlock(&mutex) == 0);
  //ret = cl->call(extent_protocol::remove, eid, r);
  return extent_protocol::OK;
}

void
extent_client::flush(lock_protocol::lockid_t lid)
{
  std::cout << "INSIDE FLUSH" << endl;
  extent_protocol::status ret;
  map<extent_protocol::extentid_t, cached_extent>::iterator it;
  string buf;

  //assert(pthread_mutex_lock(&mutex) == 0);

  it = map_client_cache.find(lid);

  assert(it != map_client_cache.end()); 

  if(it->second.to_remove){
    int r;
    ret = cl->call(extent_protocol::remove, lid, r);
    assert(ret == extent_protocol::OK);
  }
  else if(it->second.dirty){
    std:: cout << "\n\nIt is dirty";
    int r;
    buf = it->second.data; //can be optimized
    std::cout << buf << std::endl ;
    ret = cl->call(extent_protocol::put, lid, buf, r);
    assert(ret == extent_protocol::OK);
    ret = cl->call(extent_protocol::get, lid, buf);
    std::cout << "\n\nFLUSH RETURN:" << buf << "thread: " << pthread_self() << "\n";

  }

  //assert(pthread_mutex_unlock(&mutex) == 0);

}

void
extent_client::dorelease(lock_protocol::lockid_t lid)
{
  flush(lid);
}

