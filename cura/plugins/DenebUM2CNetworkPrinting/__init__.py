# SPDX-License-Identifier: LGPL-3.0-or-later
import os

from UM.Extension import Extension
from UM.Logger import Logger
from UM.Resources import Resources

DENEB_BOM_NUMBER = "deneb_um2c"
UM2C_MACHINE_TYPE = "ultimaker2_plus_connect"


class DenebUM2CNetworkPrintingExtension(Extension):
    pass


def getMetaData():
    return {}


def register(app):
    plugin_path = os.path.dirname(os.path.abspath(__file__))
    resources_path = os.path.join(plugin_path, "resources")
    Resources.addSearchPath(resources_path)
    Logger.log("i", "Deneb UM2C network printing resources registered from %s", resources_path)
    _register_deneb_bom_mapping()
    return {"extension": DenebUM2CNetworkPrintingExtension()}


def _register_deneb_bom_mapping():
    try:
        from UM3NetworkPrinting.src.Network.LocalClusterOutputDeviceManager import LocalClusterOutputDeviceManager
    except Exception:
        try:
            from plugins.UM3NetworkPrinting.src.Network.LocalClusterOutputDeviceManager import LocalClusterOutputDeviceManager
        except Exception:
            Logger.logException("w", "Could not patch Cura network printer BOM mapping for Deneb UM2C")
            return

    original_get_printer_type_identifiers = LocalClusterOutputDeviceManager._getPrinterTypeIdentifiers

    def get_printer_type_identifiers_with_deneb(*args, **kwargs):
        # Preserve the original binding contract (supports both static and
        # instance-bound methods) to avoid breakage on different Cura versions.
        identifiers = {}
        try:
            identifiers = dict(original_get_printer_type_identifiers(*args, **kwargs))
        except TypeError:
            # If the method is defined without a self/cls parameter, drop any
            # unexpectedly passed positional argument(s) from the descriptor call.
            identifiers = dict(original_get_printer_type_identifiers(
                *args[1:], **kwargs) if args else original_get_printer_type_identifiers())
        identifiers[DENEB_BOM_NUMBER] = UM2C_MACHINE_TYPE
        return identifiers

    LocalClusterOutputDeviceManager._getPrinterTypeIdentifiers = get_printer_type_identifiers_with_deneb
    Logger.log("i", "Mapped Deneb BOM %s to Cura machine type %s", DENEB_BOM_NUMBER, UM2C_MACHINE_TYPE)
