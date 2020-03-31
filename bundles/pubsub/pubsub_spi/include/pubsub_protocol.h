/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *  KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef PUBSUB_PROTOCOL_SERVICE_H_
#define PUBSUB_PROTOCOL_SERVICE_H_

#include "celix_properties.h"

#define PUBSUB_PROTOCOL_SERVICE_NAME      "pubsub_protocol"
#define PUBSUB_PROTOCOL_SERVICE_VERSION   "1.0.0"
#define PUBSUB_PROTOCOL_SERVICE_RANGE     "[1,2)"

typedef struct pubsub_protocol_header pubsub_protocol_header_t;

/**
 * The protocol header structure, contains the information about the message payload and metadata
 */
struct pubsub_protocol_header {
  /** message payload identification attributes */
    unsigned int msgId; /*!< Message id of the payload */
    unsigned short msgMajorVersion; /*!< Message Major version of the payload */
    unsigned short msgMinorVersion; /*!< Message Minor version of the payload */

    /** Payload and metadata sizes attributes */
    unsigned int payloadSize; /*!< Size of the payload (Can be used for payload buffer allocation by the receiver) */
    unsigned int metadataSize; /*!< Size of the metadata (Can be used for metadata buffer allocation by the receiver) */

    /** optional convert Endianess attribute, this attribute is used to indicate the header needs to converted for endianess during encoding
     *  this attribute is used to indicate the payload needs to converted for endianess after header decoding.
     *  Note: this attribute is not written on the line, the sync word is used to determine endianess conversion */
    unsigned int convertEndianess; /*!< Indicates that the endianess of the message needs to be converted */

    /** Optional message segmentation attributes, these attributes are only used/written by the protocol admin.
     *  When message segmentation is supported by the protocol admin */
    unsigned int payloadPartSize; /*!< payloadPartSize contains size of payload part that is send by a single segmentation message */
    unsigned int payloadOffset; /*!< payloadOffset contains offset of payload part in the total message payload */
    unsigned int isLastSegment; /*!< Indicates that message is the last message of the sequence */
};

typedef struct pubsub_protocol_payload pubsub_protocol_payload_t;

struct pubsub_protocol_payload {
    void *payload; /*!< The payload that is send by the wire protocol */
    size_t length; /*!< The length/size of the payload. */
};

typedef struct pubsub_protocol_metadata pubsub_protocol_metadata_t;

struct pubsub_protocol_metadata {
    celix_properties_t *metadata; /*!< The metadata that is send by the wire protocol */
};

typedef struct pubsub_protocol_message pubsub_protocol_message_t;

struct pubsub_protocol_message {
    pubsub_protocol_header_t header; /*!< protocol header */
    pubsub_protocol_payload_t payload; /*!< payload header */
    pubsub_protocol_metadata_t metadata; /*!< metadata header */
};

typedef struct pubsub_protocol_service {
    void* handle;
  /**
    * Returns the size of the header.
    * Is used by the receiver to configure the receive size of the header
    *
    * @param handle handle for service
    * @param length output param for header size
    * @return status code indicating failure or success
    */
    celix_status_t (*getHeaderSize)(void *handle, size_t *length);
  /**
    * Returns the size of the header buffer.
    * Is used by the receiver to configure the receive buffer size of the header
    * Note for header-less protocol (re-use header of the payload) the size is zero.
    *
    * @param handle handle for service
    * @param length output param for header buffer size
    * @return status code indicating failure or success
    */
    celix_status_t (*getHeaderBufferSize)(void *handle, size_t *length);
  /**
    * Returns the size of the sync word
    * Is used by the receiver to skip the sync in the header buffer,
    * to get in sync with data reception.
    *
    * @param handle handle for service
    * @param length output param for sync size
    * @return status code indicating failure or success
    */
    celix_status_t (*getSyncHeaderSize)(void *handle, size_t *length);
    /**
     * Returns the header (as byte array) that should be used by the underlying protocol as sync between messages.
     *
     * @param handle handle for service
     * @param sync output param for byte array
     * @return status code indicating failure or success
     */
    celix_status_t (*getSyncHeader)(void *handle, void *sync);
  /**
    * Returns the is the protocol service supports the message segmentation attributes that is used by the underlying protocol.
    *
    * @param handle handle for service
    * @param isSupported indicates that message segmentation is supported or not.
    * @return status code indicating failure or success
    */
    celix_status_t (*isMessageSegmentationSupported)(void *handle, bool *isSupported);

    /**
     * Encodes the header using the supplied message.header.
     *
     * @param handle handle for service
     * @param message message to use header from
     * @param outBuffer byte array containing the encoded header
     * @param outLength length of the byte array
     * @return status code indicating failure or success
     */
    celix_status_t (*encodeHeader)(void *handle, pubsub_protocol_message_t *message, void **outBuffer, size_t *outLength);

    /**
     * Encodes the payload using the supplied message.header. Note, this decode is for protocol specific tasks, and does not perform
     * the needed message serialization. See the serialization service for that.
     * In most cases this will simply use the known data and length from message.payload.
     *
     * @param handle handle for service
     * @param message message to use header from
     * @param outBuffer byte array containing the encoded payload
     * @param outLength length of the byte array
     * @return status code indicating failure or success
     */
    celix_status_t (*encodePayload)(void *handle, pubsub_protocol_message_t *message, void **outBuffer, size_t *outLength);

    /**
     * Encodes the metadata using the supplied message.metadata.
     *
     * @param handle handle for service
     * @param message message to use header from
     * @param outBuffer byte array containing the encoded metadata
     * @param outLength length of the byte array
     * @return status code indicating failure or success
     */
    celix_status_t (*encodeMetadata)(void *handle, pubsub_protocol_message_t *message, void **outBuffer, size_t *outLength);

    /**
     * Decodes the given data into message.header.
     *
     * @param handle handle for service
     * @param data incoming byte array to decode
     * @param length length of the byte array
     * @param message pointer to message to be filled in with decoded header
     * @return status code indicating failure or success
     */
    celix_status_t (*decodeHeader)(void* handle, void *data, size_t length, pubsub_protocol_message_t *message);

    /**
     * Decodes the given data into message.payload. Note, this decode is for protocol specific tasks, and does not perform
     * the needed message serialization. See the serialization service for that.
     * In most cases this will simply set the incoming data and length in message.payload.
     *
     * @param handle handle for service
     * @param data incoming byte array to decode
     * @param length length of the byte array
     * @param message pointer to message to be filled in with decoded payload
     * @return status code indicating failure or success
     */
    celix_status_t (*decodePayload)(void* handle, void *data, size_t length, pubsub_protocol_message_t *message);

    /**
     * Decodes the given data into message.metadata.
     *
     * @param handle handle for service
     * @param data incoming byte array to decode
     * @param length length of the byte array
     * @param message pointer to message to be filled in with decoded metadata
     * @return status code indicating failure or success
     */
    celix_status_t (*decodeMetadata)(void* handle, void *data, size_t length, pubsub_protocol_message_t *message);
} pubsub_protocol_service_t;

#endif /* PUBSUB_PROTOCOL_SERVICE_H_ */
