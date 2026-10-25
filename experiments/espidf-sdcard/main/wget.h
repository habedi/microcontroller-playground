#ifndef WGET_H
#define WGET_H

/* Downloads to path, or to standard output when path is "-". */
int wget_download(const char *url, const char *path);

#endif
