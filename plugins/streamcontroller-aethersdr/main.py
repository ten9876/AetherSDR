"""
AetherSDR StreamController Plugin
Controls FlexRadio via AetherSDR's TCI WebSocket server.

Actions:
  - PTT: Hold-to-transmit (ideal for pedal)
  - Play: Toggle slice recording playback
  - Record: Toggle slice recording
"""

import os
import sys

from src.backend.PluginManager.PluginBase import PluginBase
from src.backend.PluginManager.ActionHolder import ActionHolder
from src.backend.DeckManagement.InputIdentifier import Input
from src.backend.PluginManager.ActionInputSupport import ActionInputSupport

from .tci_client import TciClient
from .actions.PTT.PTT import PTT
from .actions.Play.Play import Play
from .actions.Record.Record import Record

sys.path.append(os.path.dirname(__file__))


class AetherSDRPlugin(PluginBase):
    def __init__(self):
        self.PLUGIN_NAME = "AetherSDR"
        self.GITHUB_REPO = "https://github.com/ten9876/AetherSDR"
        super().__init__()

        # Shared TCI client — all actions use this
        self.tci = TciClient()
        self.tci.connect("localhost", 50001)

        # PTT — hold to transmit, release to receive
        self.add_action_holder(ActionHolder(
            plugin_base=self,
            action_base=PTT,
            action_id_suffix="PTT",
            action_name="PTT (Push-to-Talk)",
            action_support={
                Input.Key: ActionInputSupport.SUPPORTED,
                Input.Dial: ActionInputSupport.UNSUPPORTED,
                Input.Touchscreen: ActionInputSupport.UNSUPPORTED,
            },
        ))

        # Play — toggle slice playback
        self.add_action_holder(ActionHolder(
            plugin_base=self,
            action_base=Play,
            action_id_suffix="Play",
            action_name="Play Recording",
            action_support={
                Input.Key: ActionInputSupport.SUPPORTED,
                Input.Dial: ActionInputSupport.UNSUPPORTED,
                Input.Touchscreen: ActionInputSupport.UNSUPPORTED,
            },
        ))

        # Record — toggle slice recording
        self.add_action_holder(ActionHolder(
            plugin_base=self,
            action_base=Record,
            action_id_suffix="Record",
            action_name="Record",
            action_support={
                Input.Key: ActionInputSupport.SUPPORTED,
                Input.Dial: ActionInputSupport.UNSUPPORTED,
                Input.Touchscreen: ActionInputSupport.UNSUPPORTED,
            },
        ))

        self.register(
            plugin_name="AetherSDR",
            github_repo="https://github.com/ten9876/AetherSDR",
            plugin_version="1.0.0",
            app_version="1.0.0-alpha",
        )
