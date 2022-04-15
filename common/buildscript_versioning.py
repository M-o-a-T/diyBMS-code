""" Script for DIYBMS """
import datetime
import subprocess
import os
from os import path

Import("env")

git_sha=None

try:
    git_sha=env["GITHUB_SHA"]
except KeyError:
    if (path.exists('..'+os.path.sep+'.git')):
        # Get the latest GIT version header/name
        try:
            git_sha = subprocess.check_output(['git','log','-1','--pretty=format:%H']).decode('utf-8')
        except Exception:
            # Ignore any error, user may not have GIT installed
            git_sha = None

# print(env.Dump())

env.Append(git_sha=git_sha)
env.Append(git_sha_short=git_sha[0:8])

include_dir = os.path.join(env.get('PROJECT_DIR'), 'include')

if (os.path.exists(include_dir) == False):
    raise Exception("Missing include folder")

with open(os.path.join(include_dir, 'EmbeddedFiles_Defines.h'), 'w') as f:
    f.write(f"""\
// This is an automatically generated file, any changes will be overwritten on compiliation!
// DO NOT CHECK THIS INTO SOURCE CONTROL

#ifndef EmbeddedFiles_Defines_H
#define EmbeddedFiles_Defines_H

static const char GIT_VERSION[] = "{git_sha or 'LocalCompile'}";
static const char GIT_VERSION_SHORT[] = "{git_sha[0:8] if git_sha else 'Local'}";
static const uint32_t GIT_VERSION_B = 0x{git_sha[0:8] if git_sha else '0'};

static const char COMPILE_DATE_TIME[] = "{datetime.datetime.utcnow().isoformat()[:-3]+'Z'}";
static const char COMPILE_DATE_TIME_SHORT[] = "{datetime.datetime.utcnow().strftime('%d %b %Y %H:%M')}";
static const uint8_t COMPILE_YEAR_BYTE = {int(datetime.datetime.utcnow().strftime('%y'))};
static const uint8_t COMPILE_WEEK_NUMBER_BYTE = {int(datetime.datetime.utcnow().strftime('%W'))};

#endif
""")
