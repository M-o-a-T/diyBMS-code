""" Script for DIYBMS """
import datetime
import subprocess
import os
from os import path

Import("env")

env.Replace(PROGNAME="diybms_controller_firmware_%s_%s" %
            (env["PIOPLATFORM"], env["PIOENV"]))


env.Replace(ESP8266_FS_IMAGE_NAME="diybms_controller_filesystemimage_%s_%s" %
            (env["PIOPLATFORM"], env["PIOENV"]))

env.Replace(ESP32_SPIFFS_IMAGE_NAME="diybms_controller_filesystemimage_%s_%s" %
            (env["PIOPLATFORM"], env["PIOENV"]))

