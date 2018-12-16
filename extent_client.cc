// RPC stubs for clients to talk to extent_server

#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

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
}

extent_protocol::status
extent_client::get(extent_protocol::extentid_t eid, std::string &buf)
{
  extent_protocol::status ret = extent_protocol::OK;

  map<extent_protocol::extentid_t, cached_extent>::iterator it;
  it = map_client_cache.find(eid);

  // if the extent is cached, read that copy
  if(it != map_client_cache.end()) {
    // if the extent was previously removed
    if(it->second.to_remove)
      ret = extent_protocol::NOENT;
    else {
      buf = it->second.data;
    }
  }
  else {
    ret = cl->call(extent_protocol::get, eid, buf);

    if(ret == extent_protocol::OK) {
      struct cached_extent c_ext;
      c_ext.data = buf;

      map_client_cache[eid] = c_ext;
    }
  }

  return ret;
}

extent_protocol::status
extent_client::getattr(extent_protocol::extentid_t eid, 
		       extent_protocol::attr &attr)
{
  extent_protocol::status ret = extent_protocol::OK;
  ret = cl->call(extent_protocol::getattr, eid, attr);
  return ret;
}

extent_protocol::status
extent_client::put(extent_protocol::extentid_t eid, std::string buf)
{
  struct cached_extent c_ext;
  c_ext.data = buf;
  c_ext.dirty = true;

  map_client_cache[eid] = c_ext;

  return extent_protocol::OK;
}

extent_protocol::status
extent_client::remove(extent_protocol::extentid_t eid)
{
  struct cached_extent c_ext;
  c_ext.to_remove = true;
  c_ext.dirty = true;

  map_client_cache[eid] = c_ext;
  //ret = cl->call(extent_protocol::remove, eid, r);
  return extent_protocol::OK;
}


