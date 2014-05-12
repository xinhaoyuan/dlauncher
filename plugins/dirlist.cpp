#include "dirlist.hpp"

#include <spawn.h>
#include <stdio.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>

#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <utime.h>

#include <string>
#include <sstream>
#include <vector>
#include <algorithm>

#define CACHE_HEAD "DIRLIST_CACHE"

using namespace std;

static int
read_utf8_string(string &s, FILE *f) {
    ostringstream oss;
    oss.str("");
    while (!feof(f)) {
        int c = fgetc(f);
        if (c < 0) return 1;
        if (c == 0) break;
        oss << (char)c;
    }
    s = oss.str();
    return 0;
}

static void
write_utf8_string(const string &s, FILE *f) {
    fwrite(s.c_str(), s.length(), 1, f);
    fputc(0, f);
}

static void
encode_path(string &result, const string &path) {
    ostringstream oss;
    for (int i = 0; i < path.length(); ++ i) {
        if (path[i] != '/' && path[i] != '_') {
            oss << path[i];
        }
        else if (path[i] != '/' || i != path.length() - 1)
        {
            oss << '_' << (path[i] == '/' ? '1' : '2');
        }
    }
    result = oss.str();
}

static void
decode_path(string &result, const string &epath) {
    ostringstream oss;
    int e = 0;
    for (int i = 0; i < epath.length(); ++ i) {
        if (epath[i] == '_')
            e = 1;
        else if (e) {
            oss << (epath[i] == '1' ? '/' : '_');
            e = 0;
        } else oss << epath[i];
    }
    result = oss.str();
}


int
dirlist(const string &dirname, vector<string> &r, const string &cache_file_prefix) {
    int cached = 0;
    string edirname;
    string cachename;
    ostringstream oss;
    
    encode_path(edirname, dirname);
    oss << cache_file_prefix << edirname;
    cachename = oss.str();

    time_t dir_time, cache_time;
    struct stat statbuf;
    
    if (stat(dirname.c_str(), &statbuf)) goto failed;
    if (!S_ISDIR(statbuf.st_mode)) goto failed;
    dir_time = statbuf.st_mtime;

    if (stat(cachename.c_str(), &statbuf) == 0) {
        // Assume the cache file is hold by plugin
        cache_time = statbuf.st_mtime;
        if (dir_time <= cache_time) cached = 1;
    }

  again:

    r.clear();

    if (cached) {
        // read the cache
        FILE *f = fopen(cachename.c_str(), "rb");
        if (f == NULL) { cached = 0; goto again; }
        string s;
        if (read_utf8_string(s, f) || s != CACHE_HEAD) {
            fclose(f);
            cached = 0;
            goto again;
        }
        while (!feof(f)) {
            if (read_utf8_string(s, f))
            {
                if (feof(f)) break;
                else { fclose(f); cached = 0; goto again; }
            }
            r.push_back(s);
        }
        fclose(f);
    } else {
        fprintf(stderr, "Building cache for %s\n", dirname.c_str());
        // build the cache
        DIR *dir = opendir(dirname.c_str());
        if (dir == NULL) goto failed;
        struct dirent *ent;
    
        while ((ent = readdir(dir)) != NULL)
        {
            if (strcmp(ent->d_name, ".") == 0) continue;
            if (strcmp(ent->d_name, "..") == 0) continue;

            r.push_back(ent->d_name);
        }

        // Using '\0' as the delimeter. We are using UTF-8 so no problem! +_+
        FILE *f = fopen(cachename.c_str(), "wb");
        if (f != NULL) {
            write_utf8_string(CACHE_HEAD, f);
            for (int i = 0; i < r.size(); ++ i)
                write_utf8_string(r[i], f);
            fclose(f);
        
            // set time stamp
            struct utimbuf times;
            times.actime = dir_time;
            times.modtime = dir_time;
            utime(cachename.c_str(), &times);
        }
        else
        {
            fprintf(stderr, "Cannot open file %s as the dir list cache\n", cachename.c_str());
        }
    }

    return 0;

  failed:
    return 1;
}
