# SPDX-License-Identifier: LGPL-3.0-or-later
import os

from UM.Logger import Logger
from UM.Resources import Resources


def getMetaData():
    return {}


def register(app):
    plugin_path = os.path.dirname(os.path.abspath(__file__))
    resources_path = os.path.join(plugin_path, "resources")
    Resources.addSearchPath(resources_path)
    Logger.log("i", "Deneb UM2C network printing resources registered from %s", resources_path)
    return {}
