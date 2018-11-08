// the extent server implementation

#include "extent_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// map for storing file/dir content
static std::map<extent_protocol::extentid_t, std::string> file_storage;
static std::map<extent_protocol::extentid_t, extent_protocol::attr> file_attr;

extent_server::extent_server() {}


int extent_server::put(extent_protocol::extentid_t id, std::string buf, int &)
{
  // store the file/dir content
  file_storage[id] = buf;

  // update attr i.e. times
  extent_protocol::attr new_attr;
  new_attr.size = 0;
  new_attr.atime = 0; //TODO: calculate these times
  new_attr.mtime = 0;
  new_attr.ctime = 0;

  file_attr[id] = new_attr;

  return extent_protocol::OK;
}

int extent_server::get(extent_protocol::extentid_t id, std::string &buf)
{
  buf = file_storage[id];

  return extent_protocol::OK;
}

int extent_server::getattr(extent_protocol::extentid_t id, extent_protocol::attr &a)
{
  int ret_val = extent_protocol::NOENT;

  std::map<extent_protocol::extentid_t, extent_protocol::attr>::iterator it;
  
  it = file_attr.find(id);

  if(it != file_attr.end()) {
    a.size = it->second.size;
    a.atime = it->second.atime;
    a.mtime = it->second.mtime;
    a.ctime = it->second.ctime;

    ret_val = extent_protocol::OK;
  }

  return ret_val;
}

int extent_server::remove(extent_protocol::extentid_t id, int &)
{
  int ret_val = extent_protocol::NOENT;

  std::map<extent_protocol::extentid_t, std::string>::iterator it_file;
  std::map<extent_protocol::extentid_t, extent_protocol::attr>::iterator it_attr;

  it_file = file_storage.find(id);

  if(it_file != file_storage.end()) {
    file_storage.erase(it_file);

    it_attr = file_attr.find(id);

    assert(it_attr != file_attr.end());
    
    file_attr.erase(it_attr);

    ret_val = extent_protocol::OK;

  }

  return ret_val;
}

