"""
MOXToggle action — toggle transmit on/off (unlike PTT which is hold-to-talk).
"""

import os
from src.backend.PluginManager.ActionBase import ActionBase
from src.backend.DeckManagement.InputIdentifier import Input


class MOXToggle(ActionBase):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._transmitting = False

    def on_ready(self):
        icon = os.path.join(self.plugin_base.PATH, "assets", "mox.png")
        if os.path.exists(icon):
            self.set_media(media_path=icon)

    def event_callback(self, event, data):
        if event != Input.Key.Events.DOWN:
            return
        self._transmitting = not self._transmitting
        self.plugin_base.tci.send(f"trx:0,{str(self._transmitting).lower()};")
