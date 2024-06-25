# IDEA: create a generic FrameHandlerAdapter that both FR and FP adapter can use (though this may not be necessary??)

"""
Created on 6th September 2017

:author: Alan Greer
"""

import logging

from odin.adapters.adapter import (
    ApiAdapter,
    ApiAdapterResponse,
    request_types,
    response_types,
    wants_metadata,
)
from odin.adapters.parameter_tree import (
    ParameterTreeError,
)
from tornado import escape
from tornado.escape import json_decode
import importlib


def _get_from_controller(controller, path, meta):
    try:
        response = controller.get(path, meta)
        status_code = 200
        logging.error("{}".format(response))
    except ParameterTreeError as param_error:
        response = {"response": "OdinDataAdapter GET error: {}".format(param_error)}
        status_code = 400
    return response, status_code


class FrameHandlerAdapter(ApiAdapter):

    def __init__(self, **kwargs):
        super(FrameHandlerAdapter, self).__init__(**kwargs)
        self._kwargs = {k: v for k, v in kwargs.items()}
        (controller_module_name, controller_class_name) = kwargs.get(
            "fh_controller"
        ).rsplit(".", 1)
        controller_module = importlib.import_module(controller_module_name)
        self._od_adapter_name = kwargs.get("od_adapter", None)
        self._fh_controller = getattr(controller_module, controller_class_name)(
            self.name
        )

    def initialize(self, adapters):
        """Initialize the adapter after it has been loaded.
        Find and record the FR adapter for later error checks
        """
        if self._od_adapter_name in adapters:
            self._od_adapter = adapters[self._od_adapter_name]
            logging.info(
                "Frame Handler adapter initiated connection to OdinData raw adapter: {}".format(
                    self._od_adapter_name
                )
            )
        else:
            logging.error(
                "Frame Handler adapter could not connect to the OdinData raw adapter: {}".format(
                    self._od_adapter_name
                )
            )

        self._controller.initialize(None, self._od_adapter)

    @request_types("application/json", "application/vnd.odin-native")
    @response_types("application/json", default="application/json")
    def get(self, path, request):
        """
        Implementation of the HTTP GET verb for OdinDataAdapter

        :param path: URI path of the GET request
        :param request: Tornado HTTP request object
        :return: ApiAdapterResponse object to be returned to the client
        """
        content_type = "application/json"

        logging.error("{}".format(path))
        response, status_code = _get_from_controller(
            self._od_adapter._controller, path, wants_metadata(request)
        )
        # get from frame handler controller instead if fails
        if status_code != 200 :
            response, status_code = _get_from_controller(
                self._controller, path, wants_metadata(request)
            )
        return ApiAdapterResponse(
            response, content_type=content_type, status_code=status_code
        )

    @request_types("application/json", "application/vnd.odin-native")
    @response_types("application/json", default="application/json")
    def put(self, path, request):  # pylint: disable=W0613
        """
        Implementation of the HTTP PUT verb for OdinDataAdapter

        :param path: URI path of the PUT request
        :param request: Tornado HTTP request object
        :return: ApiAdapterResponse object to be returned to the client
        """
        status_code = 200
        response = {}
        logging.debug("PUT path: %s", path)
        logging.debug("PUT request: %s", escape.url_unescape(request.body))

        try:
            self._od_adapter._controller.put(path, json_decode(request.body))
        except Exception:
            try:
                self._controller.put(path, json_decode(request.body))
            except Exception:
                return super(FrameProcessorAdapter, self).put(path, request)
        return ApiAdapterResponse(response, status_code=status_code)

    def cleanup(self):
        self._controller.shutdown()
