/* Copyright 2018 Streampunk Media Ltd.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include <cstddef>
#include <Processing.NDI.Lib.h>

#ifdef _WIN32
#ifdef _WIN64
#pragma comment(lib, "Processing.NDI.Lib.x64.lib")
#else // _WIN64
#pragma comment(lib, "Processing.NDI.Lib.x86.lib")
#endif // _WIN64
#endif // _WIN32

#include "grandiose_send.h"
#include "grandiose_util.h"

napi_value videoSend(napi_env env, napi_callback_info info);
napi_value audioSend(napi_env env, napi_callback_info info);
napi_value connections(napi_env env, napi_callback_info info);
napi_value tally(napi_env env, napi_callback_info info);
napi_value sourcename(napi_env env, napi_callback_info info);

void sendExecute(napi_env env, void* data) {
  sendCarrier* c = (sendCarrier *) data;

  NDIlib_send_create_t NDI_send_create_desc;

  NDI_send_create_desc.p_ndi_name = c->name;
  NDI_send_create_desc.p_groups = c->groups;
  NDI_send_create_desc.clock_video = c->clockVideo;
  NDI_send_create_desc.clock_audio = c->clockAudio;
  c->send = NDIlib_send_create(&NDI_send_create_desc);
  if (!c->send) {
    c->status = GRANDIOSE_SEND_CREATE_FAIL;
    c->errorMsg = "Failed to create NDI sender.";
    return;
  }
}

/*  implicit destruction of NDI sender via garbage collection  */
void finalizeSend(napi_env env, void* data, void* hint) {
    /*  fetch NDI sender wrapper object  */
    napi_value obj = (napi_value)hint;

    /*  fetch NDI sender external object  */
    napi_value sendValue;
    if (napi_get_named_property(env, obj, "embedded", &sendValue) != napi_ok)
        return;

    /*  ensure it was still not manually destroyed  */
    napi_valuetype result;
    if (napi_typeof(env, sendValue, &result) != napi_ok)
        return;
    if (result != napi_external)
        return;

    /*  fetch NDI sender native object  */
    void *sendData;
    if (napi_get_value_external(env, sendValue, &sendData) != napi_ok)
        return;
    NDIlib_send_instance_t send = (NDIlib_send_instance_t)sendData;

    /*  call the NDI API  */
    NDIlib_send_destroy(send);
}

/*  explicit destruction of NDI sender via "destroy" method  */
napi_value destroySend(napi_env env, napi_callback_info info) {
    /*  create a new Promise carrier object  */
    carrier *c = new carrier;
    napi_value promise;
    c->status = napi_create_promise(env, &c->_deferred, &promise);
    REJECT_RETURN;

    /*  fetch the NDI sender wrapper object ("this" of the "destroy" method)  */
    size_t argc = 1;
    napi_value args[1];
    napi_value thisValue;
    c->status = napi_get_cb_info(env, info, &argc, args, &thisValue, nullptr);
    REJECT_RETURN;

    /*  fetch NDI sender external object  */
    napi_value sendValue;
    c->status = napi_get_named_property(env, thisValue, "embedded", &sendValue);
    REJECT_RETURN;

    /*  ensure it was still not manually destroyed  */
    napi_valuetype result;
    if (napi_typeof(env, sendValue, &result) != napi_ok)
        NAPI_THROW_ERROR("NDI sender already destroyed");
    if (result == napi_external) {
        /*  fetch NDI sender native object  */
        void *sendData;
        c->status = napi_get_value_external(env, sendValue, &sendData);
        REJECT_RETURN;
        NDIlib_send_instance_t send = (NDIlib_send_instance_t)sendData;

        /*  call the NDI API  */
        NDIlib_send_destroy(send);

        /*  overwrite the "embedded" field with a non-external value
            (to ensure that the "finalizeSend" will no longer do anything
            once the garbage collection fires)  */
        napi_value value;
        napi_create_int32(env, 0, &value);
        c->status = napi_set_named_property(env, thisValue, "embedded", value);
        REJECT_RETURN;
    }

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    napi_resolve_deferred(env, c->_deferred, undefined);

    return promise;
}

void sendComplete(napi_env env, napi_status asyncStatus, void* data) {
  sendCarrier* c = (sendCarrier*) data;

  if (asyncStatus != napi_ok) {
    c->status = asyncStatus;
    c->errorMsg = "Async sender creation failed to complete.";
  }
  REJECT_STATUS;

  napi_value result;
  c->status = napi_create_object(env, &result);
  REJECT_STATUS;

  napi_value embedded;
  c->status = napi_create_external(env, c->send, finalizeSend, result, &embedded);
  REJECT_STATUS;
  c->status = napi_set_named_property(env, result, "embedded", embedded);
  REJECT_STATUS;

  napi_value destroyFn;
  c->status = napi_create_function(env, "destroy", NAPI_AUTO_LENGTH, destroySend,
    nullptr, &destroyFn);
  REJECT_STATUS;
  c->status = napi_set_named_property(env, result, "destroy", destroyFn);
  REJECT_STATUS;

  napi_value videoFn;
  c->status = napi_create_function(env, "video", NAPI_AUTO_LENGTH, videoSend,
    nullptr, &videoFn);
  REJECT_STATUS;
  c->status = napi_set_named_property(env, result, "video", videoFn);
  REJECT_STATUS;

  napi_value audioFn;
  c->status = napi_create_function(env, "audio", NAPI_AUTO_LENGTH, audioSend,
    nullptr, &audioFn);
  REJECT_STATUS;
  c->status = napi_set_named_property(env, result, "audio", audioFn);
  REJECT_STATUS;

  napi_value connectionsFn;
  c->status = napi_create_function(env, "connections", NAPI_AUTO_LENGTH, connections,
    nullptr, &connectionsFn);
  REJECT_STATUS;
  c->status = napi_set_named_property(env, result, "connections", connectionsFn);
  REJECT_STATUS;

  napi_value tallyFn;
  c->status = napi_create_function(env, "connections", NAPI_AUTO_LENGTH, tally,
    nullptr, &tallyFn);
  REJECT_STATUS;
  c->status = napi_set_named_property(env, result, "tally", tallyFn);
  REJECT_STATUS;

  napi_value sourcenameFn;
  c->status = napi_create_function(env, "sourcename", NAPI_AUTO_LENGTH, sourcename,
    nullptr, &sourcenameFn);
  REJECT_STATUS;
  c->status = napi_set_named_property(env, result, "sourcename", sourcenameFn);
  REJECT_STATUS;

  // napi_value metadataFn;
  // c->status = napi_create_function(env, "metadata", NAPI_AUTO_LENGTH, metadataReceive,
  //   nullptr, &metadataFn);
  // REJECT_STATUS;
  // c->status = napi_set_named_property(env, result, "metadata", metadataFn);
  // REJECT_STATUS;

  // napi_value dataFn;
  // c->status = napi_create_function(env, "data", NAPI_AUTO_LENGTH, dataReceive,
  //   nullptr, &dataFn);
  // REJECT_STATUS;
  // c->status = napi_set_named_property(env, result, "data", dataFn);
  // REJECT_STATUS;

  // napi_value name, groups, clockVideo, clockAudio;
  napi_value name, clockVideo, clockAudio;
  c->status = napi_create_string_utf8(env, c->name, NAPI_AUTO_LENGTH, &name);
  REJECT_STATUS;
  c->status = napi_set_named_property(env, result, "name", name);
  REJECT_STATUS;

  c->status = napi_get_boolean(env, c->clockVideo, &clockVideo);
  REJECT_STATUS;
  c->status = napi_set_named_property(env, result, "clockVideo", clockVideo);
  REJECT_STATUS;

  c->status = napi_get_boolean(env, c->clockAudio, &clockAudio);
  REJECT_STATUS;
  c->status = napi_set_named_property(env, result, "clockAudio", clockAudio);
  REJECT_STATUS;

  napi_status status;
  status = napi_resolve_deferred(env, c->_deferred, result);
  FLOATING_STATUS;

  tidyCarrier(env, c);
}

napi_value send(napi_env env, napi_callback_info info) {
  napi_valuetype type;
  sendCarrier* c = new sendCarrier;

  napi_value promise;
  c->status = napi_create_promise(env, &c->_deferred, &promise);
  REJECT_RETURN;

  size_t argc = 1;
  napi_value args[1];
  c->status = napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
  REJECT_RETURN;

  if (argc != (size_t) 1) REJECT_ERROR_RETURN(
    "Sender must be created with an object containing at least a 'name' property.",
    GRANDIOSE_INVALID_ARGS);

  c->status = napi_typeof(env, args[0], &type);
  REJECT_RETURN;
  bool isArray;
  c->status = napi_is_array(env, args[0], &isArray);
  REJECT_RETURN;
  if ((type != napi_object) || isArray) REJECT_ERROR_RETURN(
    "Single argument must be an object, not an array, containing at least a 'name' property.",
    GRANDIOSE_INVALID_ARGS);

  napi_value config = args[0];
  // napi_value name, groups, clockVideo, clockAudio;
  napi_value name, clockVideo, clockAudio;

  c->status = napi_get_named_property(env, config, "name", &name);
  REJECT_RETURN;
  c->status = napi_typeof(env, name, &type);
  REJECT_RETURN;
  if (type != napi_string) REJECT_ERROR_RETURN(
    "Name property must be of type string.",
    GRANDIOSE_INVALID_ARGS);
  size_t namel;
  c->status = napi_get_value_string_utf8(env, name, nullptr, 0, &namel);
  REJECT_RETURN;
  c->name = (char *) malloc(namel + 1);
  c->status = napi_get_value_string_utf8(env, name, c->name, namel + 1, &namel);
  REJECT_RETURN;

  // c->status = napi_get_named_property(env, config, "groups", &groups);
  // REJECT_RETURN;
  // c->status = napi_typeof(env, groups, &type);
  // REJECT_RETURN;

  // if (type != napi_undefined && type != napi_string) REJECT_ERROR_RETURN(
  //   "Groups must be of type string or ....",
  //   GRANDIOSE_INVALID_ARGS);

  c->status = napi_get_named_property(env, config, "clockVideo", &clockVideo);
  REJECT_RETURN;
  c->status = napi_typeof(env, clockVideo, &type);
  REJECT_RETURN;
  if (type != napi_undefined) {
    if (type != napi_boolean) REJECT_ERROR_RETURN(
      "ClockVideo property must be of type boolean.",
      GRANDIOSE_INVALID_ARGS);
    c->status = napi_get_value_bool(env, clockVideo, &c->clockVideo);
    REJECT_RETURN;
  }

  c->status = napi_get_named_property(env, config, "clockAudio", &clockAudio);
  REJECT_RETURN;
  c->status = napi_typeof(env, clockAudio, &type);
  REJECT_RETURN;
  if (type != napi_undefined) {
    if (type != napi_boolean) REJECT_ERROR_RETURN(
      "ClockAudio property must be of type boolean.",
      GRANDIOSE_INVALID_ARGS);
    c->status = napi_get_value_bool(env, clockAudio, &c->clockAudio);
    REJECT_RETURN;
  }

  napi_value resource_name;
  c->status = napi_create_string_utf8(env, "Send", NAPI_AUTO_LENGTH, &resource_name);
  REJECT_RETURN;
  c->status = napi_create_async_work(env, NULL, resource_name, sendExecute,
    sendComplete, c, &c->_request);
  REJECT_RETURN;
  c->status = napi_queue_async_work(env, c->_request);
  REJECT_RETURN;

  return promise;
}


void videoSendExecute(napi_env env, void* data) {
  sendDataCarrier* c = (sendDataCarrier*) data;

  NDIlib_send_send_video_v2(c->send, &c->videoFrame);
}

void videoSendComplete(napi_env env, napi_status asyncStatus, void* data) {
  sendDataCarrier* c = (sendDataCarrier*) data;
  napi_value result;
  napi_status status;

  c->status = napi_delete_reference(env, c->sourceBufferRef);
  REJECT_STATUS;

  if (asyncStatus != napi_ok) {
    c->status = asyncStatus;
    c->errorMsg = "Async video frame receive failed to complete.";
  }
  REJECT_STATUS;

  c->status = napi_create_object(env, &result);
  REJECT_STATUS;
  status = napi_resolve_deferred(env, c->_deferred, result);
  FLOATING_STATUS;

  tidyCarrier(env, c);
}

napi_value videoSend(napi_env env, napi_callback_info info) {
  napi_valuetype type;
  sendDataCarrier* c = new sendDataCarrier;

  napi_value promise;
  c->status = napi_create_promise(env, &c->_deferred, &promise);
  REJECT_RETURN;

  size_t argc = 1;
  napi_value args[1];
  napi_value thisValue;
  c->status = napi_get_cb_info(env, info, &argc, args, &thisValue, nullptr);
  REJECT_RETURN;

  napi_value sendValue;
  c->status = napi_get_named_property(env, thisValue, "embedded", &sendValue);
  REJECT_RETURN;
  void* sendData;
  c->status = napi_get_value_external(env, sendValue, &sendData);
  c->send = (NDIlib_send_instance_t) sendData;
  REJECT_RETURN;

  if (argc >= 1) {
    napi_value config;
    config = args[0];
    c->status = napi_typeof(env, config, &type);
    REJECT_RETURN;
    if (type != napi_object) REJECT_ERROR_RETURN(
      "frame must be an object",
      GRANDIOSE_INVALID_ARGS);

    bool isArray, isBuffer;
    c->status = napi_is_array(env, config, &isArray);
    REJECT_RETURN;
    if (isArray) REJECT_ERROR_RETURN(
      "Argument to video send cannot be an array.",
      GRANDIOSE_INVALID_ARGS);

    napi_value param;
    c->status = napi_get_named_property(env, config, "xres", &param);
    REJECT_RETURN;
    c->status = napi_typeof(env, param, &type);
    REJECT_RETURN;
    if (type != napi_number) REJECT_ERROR_RETURN(
      "yres value must be a number",
      GRANDIOSE_INVALID_ARGS);
    c->status = napi_get_value_int32(env, param, &c->videoFrame.xres);
    REJECT_RETURN;

    c->status = napi_get_named_property(env, config, "yres", &param);
    REJECT_RETURN;
    c->status = napi_typeof(env, param, &type);
    REJECT_RETURN;
    if (type != napi_number) REJECT_ERROR_RETURN(
      "yres value must be a number",
      GRANDIOSE_INVALID_ARGS);
    c->status = napi_get_value_int32(env, param, &c->videoFrame.yres);
    REJECT_RETURN;

    c->status = napi_get_named_property(env, config, "frameRateN", &param);
    REJECT_RETURN;
    c->status = napi_typeof(env, param, &type);
    REJECT_RETURN;
    if (type != napi_number) REJECT_ERROR_RETURN(
      "frameRateN value must be a number",
      GRANDIOSE_INVALID_ARGS);
    c->status = napi_get_value_int32(env, param, &c->videoFrame.frame_rate_N);
    REJECT_RETURN;

    c->status = napi_get_named_property(env, config, "frameRateD", &param);
    REJECT_RETURN;
    c->status = napi_typeof(env, param, &type);
    REJECT_RETURN;
    if (type != napi_number) REJECT_ERROR_RETURN(
      "frameRateD value must be a number",
      GRANDIOSE_INVALID_ARGS);
    c->status = napi_get_value_int32(env, param, &c->videoFrame.frame_rate_D);
    REJECT_RETURN;

    c->status = napi_get_named_property(env, config, "pictureAspectRatio", &param);
    REJECT_RETURN;
    c->status = napi_typeof(env, param, &type);
    REJECT_RETURN;
    if (type != napi_number) REJECT_ERROR_RETURN(
      "pictureAspectRatio value must be a number",
      GRANDIOSE_INVALID_ARGS);
    double pictureAspectRatio;
    c->status = napi_get_value_double(env, param, &pictureAspectRatio);
    REJECT_RETURN;
    c->videoFrame.picture_aspect_ratio = (float) pictureAspectRatio;

    c->videoFrame.timecode = NDIlib_send_timecode_synthesize;
    c->status = napi_get_named_property(env, config, "timecode", &param);
    REJECT_RETURN;
    c->status = napi_typeof(env, param, &type);
    REJECT_RETURN;
    if (type != napi_undefined) {
        if (type == napi_number) {
            c->status = napi_get_value_int64(env, param, &c->videoFrame.timecode);
            REJECT_RETURN;
        }
        else if (type == napi_bigint) {
            bool lossless;
            c->status = napi_get_value_bigint_int64(env, param, &c->videoFrame.timecode, &lossless);
            REJECT_RETURN;
        }
        else
            REJECT_ERROR_RETURN("timecode value must be a number or bigint", GRANDIOSE_INVALID_ARGS);
    }

    /*  initialize also timestamp (receiver-side only) and metadata  */
    c->videoFrame.timestamp = 0;
    c->videoFrame.p_metadata = NULL;

    c->status = napi_get_named_property(env, config, "frameFormatType", &param);
    REJECT_RETURN;
    c->status = napi_typeof(env, param, &type);
    REJECT_RETURN;
    if (type != napi_number) REJECT_ERROR_RETURN(
      "frameFormatType value must be a number",
      GRANDIOSE_INVALID_ARGS);
    int32_t formatType;
    c->status = napi_get_value_int32(env, param, &formatType);
    REJECT_RETURN;
    // TODO: checks
    c->videoFrame.frame_format_type = (NDIlib_frame_format_type_e) formatType;

    c->status = napi_get_named_property(env, config, "lineStrideBytes", &param);
    REJECT_RETURN;
    c->status = napi_typeof(env, param, &type);
    REJECT_RETURN;
    if (type != napi_number) REJECT_ERROR_RETURN(
      "lineStrideBytes value must be a number",
      GRANDIOSE_INVALID_ARGS);
    c->status = napi_get_value_int32(env, param, &c->videoFrame.line_stride_in_bytes);
    REJECT_RETURN;

    napi_value videoBuffer;
    c->status = napi_get_named_property(env, config, "data", &videoBuffer);
    REJECT_RETURN;
    c->status = napi_is_buffer(env, videoBuffer, &isBuffer);
    REJECT_RETURN;
    if (!isBuffer) REJECT_ERROR_RETURN(
      "data must be provided as a Node Buffer",
      GRANDIOSE_INVALID_ARGS);
    void * data;
    size_t length;
    c->status = napi_get_buffer_info(env, videoBuffer, &data, &length);
    REJECT_RETURN;
    c->videoFrame.p_data = (uint8_t*) data;
    c->status = napi_create_reference(env, videoBuffer, 1, &c->sourceBufferRef);
    REJECT_RETURN;
    // TODO: check length


    c->status = napi_get_named_property(env, config, "fourCC", &param);
    REJECT_RETURN;
    c->status = napi_typeof(env, param, &type);
    REJECT_RETURN;
    if (type != napi_number) REJECT_ERROR_RETURN(
      "fourCC value must be a number",
      GRANDIOSE_INVALID_ARGS);
    int32_t fourCC;
    c->status = napi_get_value_int32(env, param, &fourCC);
    REJECT_RETURN;
    // TODO: checks
    c->videoFrame.FourCC = (NDIlib_FourCC_video_type_e) fourCC; // TODO

  } else REJECT_ERROR_RETURN(
      "frame not provided",
    GRANDIOSE_INVALID_ARGS);

  napi_value resource_name;
  c->status = napi_create_string_utf8(env, "VideoSend", NAPI_AUTO_LENGTH, &resource_name);
  REJECT_RETURN;
  c->status = napi_create_async_work(env, NULL, resource_name, videoSendExecute,
    videoSendComplete, c, &c->_request);
  REJECT_RETURN;
  c->status = napi_queue_async_work(env, c->_request);
  REJECT_RETURN;

  return promise;
}

void audioSendExecute(napi_env env, void* data) {
  sendDataCarrier* c = (sendDataCarrier*) data;

  NDIlib_send_send_audio_v3(c->send, &c->audioFrame);
}

void audioSendComplete(napi_env env, napi_status asyncStatus, void* data) {
  sendDataCarrier* c = (sendDataCarrier*) data;
  napi_value result;
  napi_status status;

  c->status = napi_delete_reference(env, c->sourceBufferRef);
  REJECT_STATUS;

  if (asyncStatus != napi_ok) {
    c->status = asyncStatus;
    c->errorMsg = "Async audio frame send failed to complete.";
  }
  REJECT_STATUS;

  c->status = napi_create_object(env, &result);
  REJECT_STATUS;
  status = napi_resolve_deferred(env, c->_deferred, result);
  FLOATING_STATUS;

  tidyCarrier(env, c);
}

napi_value audioSend(napi_env env, napi_callback_info info) {
  napi_valuetype type;
  sendDataCarrier* c = new sendDataCarrier;

  napi_value promise;
  c->status = napi_create_promise(env, &c->_deferred, &promise);
  REJECT_RETURN;

  size_t argc = 1;
  napi_value args[1];
  napi_value thisValue;
  c->status = napi_get_cb_info(env, info, &argc, args, &thisValue, nullptr);
  REJECT_RETURN;

  napi_value sendValue;
  c->status = napi_get_named_property(env, thisValue, "embedded", &sendValue);
  REJECT_RETURN;
  void* sendData;
  c->status = napi_get_value_external(env, sendValue, &sendData);
  c->send = (NDIlib_send_instance_t) sendData;
  REJECT_RETURN;

  if (argc >= 1) {
    napi_value config;
    config = args[0];
    c->status = napi_typeof(env, config, &type);
    REJECT_RETURN;
    if (type != napi_object) REJECT_ERROR_RETURN(
      "frame must be an object",
      GRANDIOSE_INVALID_ARGS);

    bool isArray, isBuffer;
    c->status = napi_is_array(env, config, &isArray);
    REJECT_RETURN;
    if (isArray) REJECT_ERROR_RETURN(
      "Argument to audio send cannot be an array.",
      GRANDIOSE_INVALID_ARGS);

    napi_value param;
    c->status = napi_get_named_property(env, config, "sampleRate", &param);
    REJECT_RETURN;
    c->status = napi_typeof(env, param, &type);
    REJECT_RETURN;
    if (type != napi_number) REJECT_ERROR_RETURN(
      "sampleRate value must be a number",
      GRANDIOSE_INVALID_ARGS);
    c->status = napi_get_value_int32(env, param, &c->audioFrame.sample_rate);
    REJECT_RETURN;

    c->status = napi_get_named_property(env, config, "noChannels", &param);
    REJECT_RETURN;
    c->status = napi_typeof(env, param, &type);
    REJECT_RETURN;
    if (type != napi_number) REJECT_ERROR_RETURN(
      "noChannels value must be a number",
      GRANDIOSE_INVALID_ARGS);
    c->status = napi_get_value_int32(env, param, &c->audioFrame.no_channels);
    REJECT_RETURN;

    c->status = napi_get_named_property(env, config, "noSamples", &param);
    REJECT_RETURN;
    c->status = napi_typeof(env, param, &type);
    REJECT_RETURN;
    if (type != napi_number) REJECT_ERROR_RETURN(
      "Samples value must be a number",
      GRANDIOSE_INVALID_ARGS);
    c->status = napi_get_value_int32(env, param, &c->audioFrame.no_samples);
    REJECT_RETURN;

    c->audioFrame.timecode = NDIlib_send_timecode_synthesize;
    c->status = napi_get_named_property(env, config, "timecode", &param);
    REJECT_RETURN;
    c->status = napi_typeof(env, param, &type);
    REJECT_RETURN;
    if (type != napi_undefined) {
        if (type == napi_number) {
            c->status = napi_get_value_int64(env, param, &c->audioFrame.timecode);
            REJECT_RETURN;
        }
        else if (type == napi_bigint) {
            bool lossless;
            c->status = napi_get_value_bigint_int64(env, param, &c->audioFrame.timecode, &lossless);
            REJECT_RETURN;
        }
        else
            REJECT_ERROR_RETURN("timecode value must be a number or bigint", GRANDIOSE_INVALID_ARGS);
    }

    /*  initialize also timestamp (receiver-side only) and metadata  */
    c->audioFrame.timestamp = 0;
    c->audioFrame.p_metadata = NULL;

    c->status = napi_get_named_property(env, config, "channelStrideBytes", &param);
    REJECT_RETURN;
    c->status = napi_typeof(env, param, &type);
    REJECT_RETURN;
    if (type != napi_number) REJECT_ERROR_RETURN(
      "channelStrideBytes value must be a number",
      GRANDIOSE_INVALID_ARGS);
    c->status = napi_get_value_int32(env, param, &c->audioFrame.channel_stride_in_bytes);
    REJECT_RETURN;

    napi_value audioBuffer;
    c->status = napi_get_named_property(env, config, "data", &audioBuffer);
    REJECT_RETURN;
    c->status = napi_is_buffer(env, audioBuffer, &isBuffer);
    REJECT_RETURN;
    if (!isBuffer) REJECT_ERROR_RETURN(
      "data must be provided as a Node Buffer",
      GRANDIOSE_INVALID_ARGS);
    void * data;
    size_t length;
    c->status = napi_get_buffer_info(env, audioBuffer, &data, &length);
    REJECT_RETURN;
    c->audioFrame.p_data = (uint8_t *) data;
    c->status = napi_create_reference(env, audioBuffer, 1, &c->sourceBufferRef);
    REJECT_RETURN;

    c->status = napi_get_named_property(env, config, "fourCC", &param);
    REJECT_RETURN;
    c->status = napi_typeof(env, param, &type);
    REJECT_RETURN;
    if (type != napi_number) REJECT_ERROR_RETURN(
      "fourCC value must be a number",
      GRANDIOSE_INVALID_ARGS);
    int32_t fourCC;
    c->status = napi_get_value_int32(env, param, &fourCC);
    REJECT_RETURN;
    c->audioFrame.FourCC = (NDIlib_FourCC_audio_type_e)fourCC;

  } else REJECT_ERROR_RETURN(
      "frame not provided",
    GRANDIOSE_INVALID_ARGS);

  napi_value resource_name;
  c->status = napi_create_string_utf8(env, "AudioSend", NAPI_AUTO_LENGTH, &resource_name);
  REJECT_RETURN;
  c->status = napi_create_async_work(env, NULL, resource_name, audioSendExecute,
    audioSendComplete, c, &c->_request);
  REJECT_RETURN;
  c->status = napi_queue_async_work(env, c->_request);
  REJECT_RETURN;

  return promise;
}

napi_value connections(napi_env env, napi_callback_info info) {
  napi_status status;

  size_t argc = 1;
  napi_value args[1];
  napi_value thisValue;
  status = napi_get_cb_info(env, info, &argc, args, &thisValue, nullptr);
  CHECK_STATUS;

  napi_value sendValue;
  status = napi_get_named_property(env, thisValue, "embedded", &sendValue);
  CHECK_STATUS;
  void *sendData;
  status = napi_get_value_external(env, sendValue, &sendData);
  CHECK_STATUS;
  NDIlib_send_instance_t sender = (NDIlib_send_instance_t)sendData;

  int conns = NDIlib_send_get_no_connections(sender, 0);
  napi_value result;
  status = napi_create_int32(env, (int32_t)conns, &result);
  CHECK_STATUS;

  return result;
}

napi_value tally(napi_env env, napi_callback_info info) {
  napi_status status;

  size_t argc = 1;
  napi_value args[1];
  napi_value thisValue;
  status = napi_get_cb_info(env, info, &argc, args, &thisValue, nullptr);
  CHECK_STATUS;

  napi_value sendValue;
  status = napi_get_named_property(env, thisValue, "embedded", &sendValue);
  CHECK_STATUS;
  void *sendData;
  status = napi_get_value_external(env, sendValue, &sendData);
  CHECK_STATUS;
  NDIlib_send_instance_t sender = (NDIlib_send_instance_t)sendData;

  NDIlib_tally_t tally;
  bool changed = NDIlib_send_get_tally(sender, &tally, 0);

  napi_value result;
  status = napi_create_object(env, &result);
  CHECK_STATUS;

  napi_value value;
  napi_get_boolean(env, changed, &value);
  status = napi_set_named_property(env, result, "changed", value);
  CHECK_STATUS;
  napi_get_boolean(env, tally.on_program, &value);
  status = napi_set_named_property(env, result, "on_program", value);
  CHECK_STATUS;
  napi_get_boolean(env, tally.on_preview, &value);
  status = napi_set_named_property(env, result, "on_preview", value);
  CHECK_STATUS;

  return result;
}

napi_value sourcename(napi_env env, napi_callback_info info) {
  napi_status status;

  size_t argc = 1;
  napi_value args[1];
  napi_value thisValue;
  status = napi_get_cb_info(env, info, &argc, args, &thisValue, nullptr);
  CHECK_STATUS;

  napi_value sendValue;
  status = napi_get_named_property(env, thisValue, "embedded", &sendValue);
  CHECK_STATUS;
  void *sendData;
  status = napi_get_value_external(env, sendValue, &sendData);
  CHECK_STATUS;
  NDIlib_send_instance_t sender = (NDIlib_send_instance_t)sendData;

  const NDIlib_source_t *source = NDIlib_send_get_source_name(sender);
  napi_value result;
  status = napi_create_string_utf8(env, source->p_ndi_name, NAPI_AUTO_LENGTH, &result);
  CHECK_STATUS;

  return result;
}
