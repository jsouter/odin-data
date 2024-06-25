"""
Created on 30th November 2023

:author: Alan Greer
"""

import logging
import threading
import time

from odin.adapters.parameter_tree import ParameterTree


class FrameHandlerController(object):
    def __init__(self, name):
        logging.info("FrameProcessorController init called")
        self._name = name
        self._params = ParameterTree(
            {
                "module": (lambda: self._name, None, {}),
            },
            mutable=True,
        )

    def initialize(self, odin_data_adapter):
        # Record the references to the OdinDataAdapters
        # for the FrameReceivers and FrameProcessors.
        self._odin_adapter = odin_data_adapter

        # Set up the rank of the individual FP/FR applications
        # This must be called after the adapters have started
        self._thread = threading.Thread(target=self.init_rank)
        self._thread.start()

    def init_rank(self):
        # Send the setup rank after allowing time for the
        # other adapters to run up.
        while not self._odin_adapter._controller.first_update:
            time.sleep(0.1)
        self.setup_rank()

    def get(self, path, meta):
        return self._params.get(path, meta)

    def put(self, path, value):
        self._params.set(path, value)
        # TODO: If this fails, the client could see that it has changed when it hasn't
        self.process_config_changes()

    def _set(self, attr, val):
        logging.debug("_set called: {}  {}".format(attr, val))
        setattr(self, attr, val)

    def _get(self, attr):
        return lambda: getattr(self, attr)

    def setup_rank(self):
        # Attempt initialisation of the connected clients
        processes = self._odin_adapter._controller.get("count", False)["count"]
        logging.info(
            "Setting up rank information for {} FP processes".format(processes)
        )
        rank = 0
        try:
            for rank in range(processes):
                # Setup the number of processes and the rank for each client
                config = {"hdf": {"process": {"number": processes, "rank": rank}}}
                logging.debug("Sending config to FP odin adapter %i: %s", rank, config)
                self._odin_adapter_fps._controller.put(f"{rank}/config", config)

        except Exception as err:
            logging.debug("Failed to send rank information to FP applications")
            logging.error("Error: %s", err)
