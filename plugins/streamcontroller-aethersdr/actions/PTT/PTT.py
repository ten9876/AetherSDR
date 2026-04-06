"""
PTT (Push-to-Talk) action for AetherSDR StreamController plugin.
Hold pedal to transmit, release to return to receive.
"""

import os
from src.backend.PluginManager.ActionBase import ActionBase
from src.backend.DeckManagement.InputIdentifier import Input


class PTT(ActionBase):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    def on_ready(self):
        icon = os.path.join(self.plugin_base.PATH, "assets", "ptt.png")
        if os.path.exists(icon):
            self.set_media(media_path=icon)

    def event_callback(self, event, data):
        tci = self.plugin_base.tci

        if event == Input.Key.Events.DOWN:
            tci.send("trx:0,true;")
        elif event == Input.Key.Events.UP:
            tci.send("trx:0,false;")
