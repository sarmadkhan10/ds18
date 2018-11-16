/*
 * receive request from fuse and call methods of yfs_client
 *
 * started life as low-level example in the fuse distribution.  we
 * have to use low-level interface in order to get i-numbers.  the
 * high-level interface only gives us complete paths.
 */

#include <fuse_lowlevel.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <arpa/inet.h>
#include "yfs_client.h"
#include <algorithm>

using namespace std;

int myid;
yfs_client *yfs;

int id() { 
  return myid;
}

yfs_client::status
getattr(yfs_client::inum inum, struct stat &st)
{
  yfs_client::status ret;

  bzero(&st, sizeof(st));

  st.st_ino = inum;
  printf("getattr %016llx %d\n", inum, yfs->isfile(inum));
  if(yfs->isfile(inum)){
     yfs_client::fileinfo info;
     ret = yfs->getfile(inum, info);
     if(ret != yfs_client::OK)
       return ret;
     st.st_mode = S_IFREG | 0666;
     st.st_nlink = 1;
     st.st_atime = info.atime;
     st.st_mtime = info.mtime;
     st.st_ctime = info.ctime;
     st.st_size = info.size;
     printf("   getattr -> %llu\n", info.size);
   } else {
     yfs_client::dirinfo info;
     ret = yfs->getdir(inum, info);
     if(ret != yfs_client::OK)
       return ret;
     st.st_mode = S_IFDIR | 0777;
     st.st_nlink = 2;
     st.st_atime = info.atime;
     st.st_mtime = info.mtime;
     st.st_ctime = info.ctime;
     printf("   getattr -> %lu %lu %lu\n", info.atime, info.mtime, info.ctime);
   }
   return yfs_client::OK;
}


void
fuseserver_getattr(fuse_req_t req, fuse_ino_t ino,
          struct fuse_file_info *fi)
{
    struct stat st;
    yfs_client::inum inum = ino; // req->in.h.nodeid;
    yfs_client::status ret;

    ret = getattr(inum, st);
    if(ret != yfs_client::OK){
      fuse_reply_err(req, ENOENT);
      return;
    }
    fuse_reply_attr(req, &st, 0);
}

void
fuseserver_setattr(fuse_req_t req, fuse_ino_t ino, struct stat *attr, int to_set, struct fuse_file_info *fi)
{
  string value;
  printf("fuseserver_setattr 0x%x\n", to_set);

  if (FUSE_SET_ATTR_SIZE & to_set) {
    if(yfs->get_(ino, value) == yfs_client::OK) {
      // check the file size
      if(yfs->isfile(ino)) {
      	// truncate the value
        if(attr->st_size <= value.size()) {
          value = value.substr(0, max((off_t) 0, attr->st_size-1));
        }
        // pad with '\0'
        else {
          unsigned int to_pad = attr->st_size - value.size();

          value += string(to_pad, '\0');
        }

        cout << "size: " << value.size() << " st_size: " << attr->st_size << endl;

        assert(value.size() == attr->st_size);

        assert(yfs->write_file(ino, value.c_str(), attr->st_size, 0, true) == yfs_client::OK);

      	struct stat st;
        if(getattr(ino, st) != yfs_client::OK) assert("setattr can't get attr");
      	fuse_reply_attr(req, &st, 0);
      	printf("   fuseserver_setattr set size to %zu\n", attr->st_size);

      } else {
        // not doing it for dir
        assert("setattr for a dir called");
      }
      //if a directory --not yet sure what the size should be
      //conservatively setting it to size of value
      /*else{
      	attr->st_size = value.size();
      	struct stat st;
      	fuse_reply_attr(req, &st, 0);
      	printf("   fuseserver_setattr set size to %zu\n", attr->st_size);
      }*/
    } else {
      fuse_reply_err(req, ENOENT);
    }
  }
}

void
fuseserver_read(fuse_req_t req, fuse_ino_t ino, size_t size,
      off_t off, struct fuse_file_info *fi)
{
  // You fill this in
  cout << "fuseserver_read called" << endl;
  string read_content;

  if(yfs->read_file(ino, size, off, read_content) == yfs_client::OK) {
    fuse_reply_buf(req, read_content.c_str(), size);
  }
  else
    fuse_reply_err(req, ENOENT);
}

void
fuseserver_write(fuse_req_t req, fuse_ino_t ino,
  const char *buf, size_t size, off_t off,
  struct fuse_file_info *fi)
{
  // You fill this in
  cout << "fuseserver_write called" << endl;
  if(yfs->write_file(ino, buf, size, off, false) == yfs_client::OK)
    fuse_reply_write(req, size);
  else
    fuse_reply_err(req, ENOENT);
}

yfs_client::status
fuseserver_createhelper(fuse_ino_t parent, const char *name,
     mode_t mode, struct fuse_entry_param *e)
{
  unsigned long long ino;
  struct stat attr;
  yfs_client::status ret;
  int isfile = 1;

  memset(&attr, 0, sizeof(attr));

  ret = yfs->createhelper(parent, name, &ino, isfile); 

  attr.st_ino = ino;
  attr.st_mode = S_IFREG | 0666;
  attr.st_nlink = 1;

  e->ino = ino;
  e->attr = attr;
  e->attr_timeout = 0.0;
  e->entry_timeout = 0.0;
  e->generation = 1;

  cout << "fuse createhelper ret: " << ret << endl;

  return ret;
}

void
fuseserver_create(fuse_req_t req, fuse_ino_t parent, const char *name,
   mode_t mode, struct fuse_file_info *fi)
{
  struct fuse_entry_param e;
  if( fuseserver_createhelper( parent, name, mode, &e ) == yfs_client::OK ) {
    fuse_reply_create(req, &e, fi);
  } else {
    fuse_reply_err(req, ENOENT);
  }
}

void fuseserver_mknod( fuse_req_t req, fuse_ino_t parent, 
    const char *name, mode_t mode, dev_t rdev ) {
  struct fuse_entry_param e;
  if( fuseserver_createhelper( parent, name, mode, &e ) == yfs_client::OK ) {
    fuse_reply_entry(req, &e);
  } else {
    fuse_reply_err(req, ENOENT);
  }
}

void
fuseserver_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
  struct fuse_entry_param e;
  bool found = false;
  unsigned long long inum_;
  int size_;

  e.attr_timeout = 0.0;
  e.entry_timeout = 0.0;

  // You fill this in:
  // Look up the file named `name' in the directory referred to by
  // `parent' in YFS. If the file was found, initialize e.ino and
  // e.attr appropriately.
  found = yfs->lookup(parent, name, &inum_, &size_);

  e.ino = inum_;

  if(yfs->isfile(inum_)) {
    e.attr.st_mode = S_IFREG | 0666;
    e.attr.st_nlink = 1;
    e.attr.st_size = size_;
  } else {
    e.attr.st_mode = S_IFDIR | 0777;
    e.attr.st_nlink = 2;
  }

  e.attr.st_ino = inum_;
  e.attr.st_size = size_;

  if (found)
    fuse_reply_entry(req, &e);
  else
    fuse_reply_err(req, ENOENT);
}


struct dirbuf {
    char *p;
    size_t size;
};

void dirbuf_add(struct dirbuf *b, const char *name, fuse_ino_t ino)
{
    struct stat stbuf;
    size_t oldsize = b->size;
    b->size += fuse_dirent_size(strlen(name));
    b->p = (char *) realloc(b->p, b->size);
    memset(&stbuf, 0, sizeof(stbuf));
    stbuf.st_ino = ino;
    fuse_add_dirent(b->p + oldsize, name, &stbuf, b->size);
}

#define min(x, y) ((x) < (y) ? (x) : (y))

int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize,
          off_t off, size_t maxsize)
{
  if ((size_t)off < bufsize)
    return fuse_reply_buf(req, buf + off, min(bufsize - off, maxsize));
  else
    return fuse_reply_buf(req, NULL, 0);
}

void
fuseserver_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
          off_t off, struct fuse_file_info *fi)
{
  yfs_client::inum inum = ino; // req->in.h.nodeid;
  struct dirbuf b;
  yfs_client::dirent e;

  printf("fuseserver_readdir\n");

  if(!yfs->isdir(inum)){
    fuse_reply_err(req, ENOTDIR);
    return;
  }

  cout << "fuseserver_readdir is a dir: " << ino << endl;

  memset(&b, 0, sizeof(b));


  string value;
  yfs->get_(ino, value);

  if(!value.empty()) {

    istringstream iss(value);
    string file_entry;
    while (getline(iss, file_entry, ';'))
    {
      int l = file_entry.find(".");
      string file_name_;
      char file_name[128];
      file_name_ = file_entry.substr(0, l);
      
      strcpy(file_name, file_name_.c_str());

      fuse_ino_t inum_ = stoull(file_entry.substr(l+1));

      cout << "ccfilename: " << file_name << " inum: " << inum_ << endl;

      dirbuf_add(&b, file_name, inum_);
    }
  }
  else
    cout << "empty readdir" << endl;

  reply_buf_limited(req, b.p, b.size, off, size);
  free(b.p);
}


void
fuseserver_open(fuse_req_t req, fuse_ino_t ino,
     struct fuse_file_info *fi)
{
  cout << "fuseopen. ino: " << ino << endl;

  if(yfs->open_file(ino)){
    //fi->fh = ino;

    cout << "fuseopen open file success. ino: " << ino << endl;

    fuse_reply_open(req, fi);
  }
  else
    fuse_reply_err(req, EIO);
}

void
fuseserver_mkdir(fuse_req_t req, fuse_ino_t parent, const char *name,
     mode_t mode)
{

  unsigned long long ino;
  struct stat attr;
  yfs_client::status ret;
  int isfile = 0;

  struct fuse_entry_param e;
  cout << "\n make dir called";

  memset(&attr, 0, sizeof(attr));

  if(ret = yfs->createhelper(parent, name, &ino, isfile) != yfs_client::OK)
  {
    fuse_reply_err(req, ENOSYS);
    return;
  }

  attr.st_ino = ino;
  attr.st_mode = S_IFDIR | 0777;
  attr.st_nlink = 2;

  e.ino = ino;
  e.attr = attr;
  e.attr_timeout = 0.0;
  e.entry_timeout = 0.0;
  e.generation = 1;

  cout << "\n returned from MKDIR";
  fuse_reply_entry(req, &e);
}

void
fuseserver_unlink(fuse_req_t req, fuse_ino_t parent, const char *name)
{
  cout << "unlink called." << endl;
  // You fill this in
  // Success:	fuse_reply_err(req, 0);
  // Not found:	fuse_reply_err(req, ENOENT);
  fuse_reply_err(req, ENOSYS);
}

void
fuseserver_statfs(fuse_req_t req)
{
  struct statvfs buf;

  printf("statfs\n");

  memset(&buf, 0, sizeof(buf));

  buf.f_namemax = 255;
  buf.f_bsize = 512;

  fuse_reply_statfs(req, &buf);
}

struct fuse_lowlevel_ops fuseserver_oper;

int
main(int argc, char *argv[])
{
  char *mountpoint = 0;
  int err = -1;
  int fd;

  setvbuf(stdout, NULL, _IONBF, 0);

  if(argc != 4){
    fprintf(stderr, "Usage: yfs_client <mountpoint> <port-extent-server> <port-lock-server>\n");
    exit(1);
  }
  mountpoint = argv[1];

  srandom(getpid());

  myid = random();

  yfs = new yfs_client(argv[2], argv[3]);

  fuseserver_oper.getattr    = fuseserver_getattr;
  fuseserver_oper.statfs     = fuseserver_statfs;
  fuseserver_oper.readdir    = fuseserver_readdir;
  fuseserver_oper.lookup     = fuseserver_lookup;
  fuseserver_oper.create     = fuseserver_create;
  fuseserver_oper.mknod      = fuseserver_mknod;
  fuseserver_oper.open       = fuseserver_open;
  fuseserver_oper.read       = fuseserver_read;
  fuseserver_oper.write      = fuseserver_write;
  fuseserver_oper.setattr    = fuseserver_setattr;
  fuseserver_oper.unlink     = fuseserver_unlink;
  fuseserver_oper.mkdir      = fuseserver_mkdir;

  const char *fuse_argv[20];
  int fuse_argc = 0;
  fuse_argv[fuse_argc++] = argv[0];
#ifdef __APPLE__
  fuse_argv[fuse_argc++] = "-o";
  fuse_argv[fuse_argc++] = "nolocalcaches"; // no dir entry caching
  fuse_argv[fuse_argc++] = "-o";
  fuse_argv[fuse_argc++] = "daemon_timeout=86400";
#endif

  // everyone can play, why not?
  //fuse_argv[fuse_argc++] = "-o";
  //fuse_argv[fuse_argc++] = "allow_other";

  fuse_argv[fuse_argc++] = mountpoint;
  fuse_argv[fuse_argc++] = "-d";

  fuse_args args = FUSE_ARGS_INIT( fuse_argc, (char **) fuse_argv );
  int foreground;
  int res = fuse_parse_cmdline( &args, &mountpoint, 0 /*multithreaded*/, 
        &foreground );
  if( res == -1 ) {
    fprintf(stderr, "fuse_parse_cmdline failed\n");
    return 0;
  }
  
  args.allocated = 0;

  fd = fuse_mount(mountpoint, &args);
  if(fd == -1){
    fprintf(stderr, "fuse_mount failed\n");
    exit(1);
  }

  struct fuse_session *se;

  se = fuse_lowlevel_new(&args, &fuseserver_oper, sizeof(fuseserver_oper),
       NULL);
  if(se == 0){
    fprintf(stderr, "fuse_lowlevel_new failed\n");
    exit(1);
  }

  struct fuse_chan *ch = fuse_kern_chan_new(fd);
  if (ch == NULL) {
    fprintf(stderr, "fuse_kern_chan_new failed\n");
    exit(1);
  }

  fuse_session_add_chan(se, ch);
  // err = fuse_session_loop_mt(se);   // FK: wheelfs does this; why?
  err = fuse_session_loop(se);
    
  fuse_session_destroy(se);
  close(fd);
  fuse_unmount(mountpoint);

  return err ? 1 : 0;
}
