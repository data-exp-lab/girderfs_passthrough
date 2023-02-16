#define FUSE_USE_VERSION 31
#ifdef linux
/* For pread()/pwrite()/utimensat() */
#define _XOPEN_SOURCE 700
#endif
#define _GNU_SOURCE
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#include <fuse.h>
#include <sys/stat.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>
#include <dirent.h>
#include <unistd.h>
#include <curl/curl.h>
#include <jansson.h>
#include "tree.h"

static struct options {
	const char *girderFolderListing;
	const char *girderToken;
	int show_help;
} options;

#define OPTION(t, p)                           \
    { t, offsetof(struct options, p), 1 }
static const struct fuse_opt option_spec[] = {
	OPTION("--girder_url=%s", girderFolderListing),
	OPTION("--token=%s", girderToken),
	OPTION("-h", show_help),
	OPTION("--help", show_help),
	FUSE_OPT_END
};

#define tree_root                       ((GIRDERFS *)fuse_get_context()->private_data)->root

#define fi_dirbit                       (0x8000000000000000ULL)
#define fi_fh(fi, MASK)                 ((fi)->fh & (MASK))
#define fi_setfh(fi, FH, MASK)          ((fi)->fh = (intptr_t)(FH) | (MASK))
#define fi_fd(fi)                       (fi_fh(fi, fi_dirbit) ? \
    dirfd((DIR *)(intptr_t)fi_fh(fi, ~fi_dirbit)) : (int)fi_fh(fi, ~fi_dirbit))
#define fi_dirp(fi)                     ((DIR *)(intptr_t)fi_fh(fi, ~fi_dirbit))
#define fi_setfd(fi, fd)                (fi_setfh(fi, fd, 0))
#define fi_setdirp(fi, dirp)            (fi_setfh(fi, dirp, fi_dirbit))


typedef struct
{
    struct TreeNode* root;
} GIRDERFS;

struct string {
  char *ptr;
  size_t len;
};

void init_string(struct string *s) {
  s->len = 0;
  s->ptr = malloc(s->len + 1);
  if (s->ptr == NULL) {
    fprintf(stderr, "malloc() failed\n");
    exit(EXIT_FAILURE);
  }
  s->ptr[0] = '\0';
}

size_t writefunc(void *ptr, size_t size, size_t nmemb, struct string *s)
{
  size_t new_len = s->len + size * nmemb;
  s->ptr = realloc(s->ptr, new_len + 1);
  if (s->ptr == NULL) {
    fprintf(stderr, "realloc() failed\n");
    exit(EXIT_FAILURE);
  }
  memcpy(s->ptr + s->len, ptr, size * nmemb);
  s->ptr[new_len] = '\0';
  s->len = new_len;

  return size * nmemb;
}

static void *passthrough_init(struct fuse_conn_info *conn,
			struct fuse_config *cfg)
{
	(void) conn;
	cfg->kernel_cache = 1;
	return fuse_get_context()->private_data;
}

static int passthrough_getattr(const char *path, struct stat *stbuf,
			 struct fuse_file_info *fi)
{
	(void) fi;
	int res = 0;

  struct TreeNode* node = getNodeByPath(tree_root, path);
  if (!node) {
    return -ENOENT;
  }
  if (node->host_path) {
    return -1 != lstat(node->host_path, stbuf) ? 0 : -errno;
  } else {
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2;
  }

	return res;
}

static int passthrough_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi,
			 enum fuse_readdir_flags flags)
{
	(void) offset;
	(void) fi;
	(void) flags;

  struct TreeNode* node = getNodeByPath(tree_root, path);
  struct stat stbuf;
  if (node) {
    filler(buf, ".", NULL, 0, 0);
	  filler(buf, "..", NULL, 0, 0);
    for (int i = 0; i < node->num_children; i++) {
        if (node->children[i]->host_path) {
          stbuf.st_mode = S_IFREG | 0444;
          stbuf.st_nlink = 1;
        } else {
          stbuf.st_mode = S_IFDIR | 0755;
          stbuf.st_nlink = 2;
        }
        filler(buf, node->children[i]->name, &stbuf, 0, 0);
    }
  } else {
    return -ENOENT;
  }

	return 0;
}

static int passthrough_open(const char *path, struct fuse_file_info *fi)
{
  int fd;
  return -1 != (fd = open(getHostPath(tree_root, path), fi->flags)) ? (fi_setfd(fi, fd), 0) : -errno;
}

static int passthrough_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
  int fd = fi_fd(fi);
  int nb;
  return -1 != (nb = pread(fd, buf, size, offset)) ? nb : -errno;
}

static const struct fuse_operations passthrough_oper = {
	.init     = passthrough_init,
	.getattr	= passthrough_getattr,
	.readdir	= passthrough_readdir,
	.open		  = passthrough_open,
	.read		  = passthrough_read,
};

static void show_help(const char *progname)
{
	printf("usage: %s [options] <mountpoint>\n\n", progname);
	printf("File-system specific options:\n"
	       "    --girder_url=<s>   Url pointing to <girder>/api/v1/folder/:id/listing\n"
	       "    --token=<s>        Girder Token\n"
	       "\n");
}

int main(int argc, char *argv[])
{
	int ret;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  GIRDERFS fsdata;
  CURL *curl;
  CURLcode res;
  struct string s;
  struct TreeNode* tree;
  json_t *root;
  json_error_t error;

	options.girderFolderListing = strdup("https://girder.local.wholetale.org/api/v1/user/me");
	options.girderToken = strdup("faketoken");

	if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1)
		return 1;

	if (options.show_help) {
		show_help(argv[0]);
		assert(fuse_opt_add_arg(&args, "--help") == 0);
		args.argv[0][0] = '\0';
	}

  init_string(&s);
  curl = curl_easy_init();
  if (curl) {
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Girder-Token: ");
    char *token_header = malloc(strlen(options.girderToken) + 16);
    if (token_header == NULL) {
      fprintf(stderr, "malloc() failed\n");
      exit(EXIT_FAILURE);
    }
    sprintf(token_header, "Girder-Token: %s", options.girderToken);
    headers = curl_slist_append(headers, token_header);
    free(token_header);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_URL, options.girderFolderListing);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);

    /* Check for errors */
    if (res != CURLE_OK) {
      fprintf(stderr, "curl_easy_perform() failed: %s\n",
              curl_easy_strerror(res));
      exit(EXIT_FAILURE);
    }

    /* always cleanup */
    curl_easy_cleanup(curl);
  }

  root = json_loads(s.ptr, 0, &error);
  if (!root) {
    fprintf(stderr, "error: on line %d: %s\n", error.line, error.text);
    exit(EXIT_FAILURE);
  }

  /* Use the parsed JSON data here */
  tree = readTreeFromJson(root);
  printTree(tree, 0);

  fsdata.root = tree;

  /* free resources that we no longer need */
  json_decref(root);
  free(s.ptr);

	ret = fuse_main(args.argc, args.argv, &passthrough_oper, &fsdata);
	fuse_opt_free_args(&args);
	return ret;
}
