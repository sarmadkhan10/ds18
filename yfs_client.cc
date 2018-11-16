// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <random>
#include <string>
#include <sstream>

using namespace std;

static std::mt19937_64 mt_rand_gen(time(0));

yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
  ec = new extent_client(extent_dst);
  assert(pthread_mutex_init(&m_, 0) == 0);

}

yfs_client::inum
yfs_client::n2i(std::string n)
{
  std::istringstream ist(n);
  unsigned long long finum;
  ist >> finum;
  return finum;
}

std::string
yfs_client::filename(inum inum)
{
  std::ostringstream ost;
  ost << inum;
  return ost.str();
}

bool
yfs_client::isfile(inum inum)
{
  if(inum & 0x80000000)
    return true;
  return false;
}

bool
yfs_client::isdir(inum inum)
{
  return ! isfile(inum);
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
  int r = OK;


  printf("getfile %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  fin.atime = a.atime;
  fin.mtime = a.mtime;
  fin.ctime = a.ctime;
  fin.size = a.size;
  printf("getfile %016llx -> sz %llu\n", inum, fin.size);

 release:

  return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
  int r = OK;


  printf("getdir %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }
  din.atime = a.atime;
  din.mtime = a.mtime;
  din.ctime = a.ctime;

 release:
  return r;
}

/*
 *fuse lookup invokes this lookup 
 *look for parent inum in the map and returns its value string
 * search for the file specified by the name and set attributes and return if found
 * return 0 otherwise
 *
 *return value: 1 if record found 0 otherwise
 *@param: forwarding all the parameter values from fuse lookup
 */

int 
yfs_client::lookup(unsigned long long parent, const char *name, unsigned long long *inum_, int *size_)
{
  string value;
  int loc;
  unsigned long long inum = 0;
  ostringstream out;
  extent_protocol::attr a;

  // initially set it to zero
  *inum_ = 0;

  printf("client lookup enter ");
  std::cout << name << std::endl;

  // verify parent is a directory
  if(!isdir(parent)) return false;

  if(ec->get(parent, value) != yfs_client::OK) return false;

  if(value.empty()) return false;

  loc = value.find(name, 0);
  if(loc != string::npos)
    inum = stoull(strtok(&value[value.find(".", loc+1)+1], ";"));
  else
    return false;

  *inum_ = inum;

  // get attr
  if(ec->getattr(inum, a) != yfs_client::OK) return false;

  *size_ = a.size;

  cout << "client lookup successful exit. " << name << " " << "inum: " <<  inum << endl;

  return true;
}
/*
 *fuse createhelper invokes this 
 *
 * looks up if file already exists if not generate a random inum and  append the 
 * file inum and name to parent directory structure
 *
 *return status 
 *@param parent parent directory  
 *@param parent parent directory  
 *@param name name of the file  
 *@param fuse_entry_param entire attribute   
 */
yfs_client::status
yfs_client::createhelper(unsigned long long parent, const char *name, unsigned long long *ino_new, int createfile)
{
  printf("\ncreatehelper ");
  std::cout << "parent " << parent << " name " << name << endl;

  string value;
  string temp;
  unsigned long long inum_;
  int size;
  ostringstream out;

  //check if file already present
  if(lookup(parent, name, &inum_, &size)) {
    cout << "createhelper " << parent << " " << name << " already present!" << endl;
    *ino_new = inum_;
    return yfs_client::OK;
  }

  cout << "createhelper " << parent << " " << name << "not already present." << endl;
  
  if(ec->get(parent, value) != yfs_client::OK) {
    cout << "createhelper " << parent << " does not exist." << endl;
    return yfs_client::NOENT;
  }

  cout << "createhelper " << parent << " " << "get value done." << endl;

  //genereate a 64 bit random number and check if has 31st bit one (recognising a file)
  if(createfile)
    do {
      inum_ = mt_rand_gen();
    } while(!isfile(inum_));
  else
   do {
     inum_ = mt_rand_gen();
   } while(isfile(inum_));

  // create a new entry for file
  ec->put(inum_, "");

  *ino_new = inum_;

  // update parent dir entry
  out << name << "." << inum_ <<";";
  value += out.str();
  ec->put(parent, value);

  cout << "createhelper successful exit." << endl;

  return yfs_client::OK;
//include based on test results
#if 0 
  
  getattr(&a);
  
  e->st_mode = S_IFREG | 0666;
  e->st_nlink = 1;
  e->st_atime = a.atime;
  e->st_mtime = a.mtime;
  e->st_ctime = a.ctime;
  e->st_size = a.size ;

#endif

}
/*
 *fuse open invokes this 
 *
 * looks up if file already exists 
 *
 *return true if file exists and opened 
 *@param ino 64bit fuse inum
 *@param fi file information structure
 */
bool
yfs_client::open_file(unsigned long long ino)
{
  string value;
  ostringstream out;
  printf("\n on open file client");

  // if file is not present, return false
  if(ec->get(ino, value) != yfs_client::OK) return false;
  else return true;

//include based on test results
#if 0 
  
  getattr(&a);
  
  e->st_mode = S_IFREG | 0666;
  e->st_nlink = 1;
  e->st_atime = a.atime;
  e->st_mtime = a.mtime;
  e->st_ctime = a.ctime;
  e->st_size = a.size ;

#endif

  return true;
}
int yfs_client::get_(unsigned long long inum_, string &buf){

  return ec->get(inum_, buf);
}

// writes to the file inum if it exists
int
yfs_client::write_file(unsigned long long inum, const char* buf, size_t len, off_t offset, bool trunc) {

  // get the prev file content (if it exists)
  string prev_val;
  if(ec->get(inum, prev_val) == yfs_client::NOENT) return yfs_client::NOENT;

  assert(offset <= prev_val.size());

  // construct the new content of the file
  string new_val;

  if(trunc) {
    new_val = prev_val.substr(0, offset);
    new_val += string(buf, len);
  } else
    new_val = prev_val.replace(offset, len, string(buf, len));

  return ec->put(inum, new_val);
}

// reads from a file if it exists
int
yfs_client::read_file(unsigned long long inum, size_t len, off_t offset, string &buf) {
  // get the file content (if it exists)
  string file_content;
  if(ec->get(inum, file_content) == yfs_client::NOENT) return yfs_client::NOENT;

  // read file
  string read_content = file_content.substr(offset, len);

  // check if EOF reached
  if(read_content.size() < len) {
    //TODO: handle this case
    assert("read file beyond not implemented");
  }

  buf = read_content;

  return yfs_client::OK;
}

bool
yfs_client::remove_file(unsigned long long inum_parent, unsigned long long inum_file,
                        const char* filename) {
  string parent_content;
  int index;

  if(ec->get(inum_parent, parent_content) == yfs_client::NOENT) return false;

  string to_remove(string(filename) + "." + to_string(inum_file) + ";");

  index = parent_content.find(to_remove);

  if(index == string::npos) return false;

  parent_content.erase(index, to_remove.size());

  if(ec->put(inum_parent, parent_content) != yfs_client::OK) return false;

  if(ec->remove(inum_file) != yfs_client::OK) return false;

  return true;
}