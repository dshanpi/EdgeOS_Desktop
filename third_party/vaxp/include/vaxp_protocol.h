#ifndef VAXP_PROTOCOL_H
#define VAXP_PROTOCOL_H

/*
 * VAXP 1.0 - Vision AI eXchange Protocol
 * Core wire protocol definitions.
 *
 * Wire byte order: little-endian.
 * Base header size: 20 bytes.
 * CRC: CRC16-CCITT-FALSE over bytes [Version .. PayloadEnd], excluding Magic and CRC field.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VAXP_PROTOCOL_VERSION_MAJOR      1u
#define VAXP_PROTOCOL_VERSION_MINOR      0u
#define VAXP_PROTOCOL_VERSION            0x10u

#define VAXP_MAGIC0                      0xAAu
#define VAXP_MAGIC1                      0x55u

#define VAXP_BASE_HEADER_SIZE            20u
#define VAXP_MAX_HEADER_SIZE             64u
#define VAXP_CRC_SIZE                    2u
#define VAXP_DEFAULT_MAX_PAYLOAD         4096u
#define VAXP_DEFAULT_HEARTBEAT_MS        2000u
#define VAXP_DEFAULT_MAX_PENDING         8u

#define VAXP_VERSION_MAJOR(v)            ((((uint8_t)(v)) >> 4) & 0x0Fu)
#define VAXP_VERSION_MINOR(v)            (((uint8_t)(v)) & 0x0Fu)
#define VAXP_MAKE_VERSION(major, minor)  ((uint8_t)((((major) & 0x0Fu) << 4) | ((minor) & 0x0Fu)))

#define VAXP_ADDR_HOST                   0x00u
#define VAXP_ADDR_DEVICE_MIN             0x01u
#define VAXP_ADDR_DEVICE_MAX             0xEFu
#define VAXP_ADDR_RESERVED               0xF0u
#define VAXP_ADDR_BROADCAST              0xFFu

#if defined(_MSC_VER)
#  define VAXP_PACKED_BEGIN __pragma(pack(push, 1))
#  define VAXP_PACKED_END   __pragma(pack(pop))
#  define VAXP_PACKED
#elif defined(__GNUC__) || defined(__clang__)
#  define VAXP_PACKED_BEGIN
#  define VAXP_PACKED_END
#  define VAXP_PACKED __attribute__((packed))
#else
#  define VAXP_PACKED_BEGIN
#  define VAXP_PACKED_END
#  define VAXP_PACKED
#endif

#if defined(__cplusplus)
#  define VAXP_STATIC_ASSERT(cond, msg) static_assert((cond), msg)
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#  define VAXP_STATIC_ASSERT(cond, msg) _Static_assert((cond), msg)
#else
#  define VAXP_STATIC_ASSERT_GLUE_(a, b) a##b
#  define VAXP_STATIC_ASSERT_GLUE(a, b) VAXP_STATIC_ASSERT_GLUE_(a, b)
#  define VAXP_STATIC_ASSERT(cond, msg) typedef char VAXP_STATIC_ASSERT_GLUE(vaxp_static_assert_, __LINE__)[(cond) ? 1 : -1]
#endif

typedef enum VaxpMessageType {
    VAXP_MSG_REQUEST  = 0x01,
    VAXP_MSG_RESPONSE = 0x02,
    VAXP_MSG_EVENT    = 0x03,
    VAXP_MSG_ACK      = 0x04
} VaxpMessageType;

enum {
    VAXP_FLAG_ACK_REQUIRED  = 0x01u,
    VAXP_FLAG_URGENT        = 0x02u,
    VAXP_FLAG_FRAGMENT      = 0x04u,
    VAXP_FLAG_LAST_FRAGMENT = 0x08u,
    VAXP_FLAG_PERSIST       = 0x10u,
    VAXP_FLAG_COMPRESSED    = 0x20u,
    VAXP_FLAG_ENCRYPTED     = 0x40u,
    VAXP_FLAG_RESERVED      = 0x80u
};

/*
 * These flags change the payload representation.  A receiver that does not
 * implement their corresponding transform must reject the frame before
 * parsing, acknowledging, or dispatching its payload.
 */
#define VAXP_FLAG_UNSUPPORTED_PAYLOAD                                      \
    (VAXP_FLAG_FRAGMENT | VAXP_FLAG_LAST_FRAGMENT | VAXP_FLAG_COMPRESSED | \
     VAXP_FLAG_ENCRYPTED | VAXP_FLAG_RESERVED)

typedef enum VaxpStatus {
    VAXP_STATUS_OK          = 0,
    VAXP_STATUS_ACCEPTED    = 1,
    VAXP_STATUS_IN_PROGRESS = 2,
    VAXP_STATUS_PARTIAL     = 3
} VaxpStatus;

typedef enum VaxpError {
    VAXP_OK                          = 0,

    VAXP_ERR_UNKNOWN_COMMAND         = -1,
    VAXP_ERR_INVALID_LENGTH          = -2,
    VAXP_ERR_INVALID_PARAMETER       = -3,
    VAXP_ERR_CRC                     = -4,
    VAXP_ERR_TIMEOUT                 = -5,
    VAXP_ERR_NOT_SUPPORTED           = -6,
    VAXP_ERR_BUSY                    = -7,
    VAXP_ERR_BAD_STATE               = -8,
    VAXP_ERR_VERSION_NOT_SUPPORTED   = -9,
    VAXP_ERR_SESSION_INVALID         = -10,

    VAXP_ERR_DEVICE_NOT_READY        = -100,
    VAXP_ERR_OVER_TEMPERATURE        = -101,

    VAXP_ERR_CAMERA_NOT_FOUND        = -200,
    VAXP_ERR_CAMERA_NO_SIGNAL        = -201,

    VAXP_ERR_MODEL_NOT_FOUND         = -300,
    VAXP_ERR_MODEL_LOAD_FAILED       = -301,
    VAXP_ERR_MODEL_INVALID           = -302,

    VAXP_ERR_PIPELINE_NOT_FOUND      = -400,
    VAXP_ERR_PIPELINE_RUNNING        = -401,

    VAXP_ERR_RULE_NOT_FOUND          = -500,

    VAXP_ERR_FILE_IO                 = -600,
    VAXP_ERR_IMAGE_VERIFY            = -601,

    VAXP_ERR_AUTH_REQUIRED           = -700,
    VAXP_ERR_AUTH_FAILED             = -701,

    VAXP_ERR_NO_MEMORY               = -800,
    VAXP_ERR_RESOURCE_LIMIT          = -801,

    VAXP_ERR_INTERNAL                = -900
} VaxpError;

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpHeader {
    uint8_t  magic[2];
    uint8_t  version;
    uint8_t  header_length;
    uint8_t  message_type;
    uint8_t  flags;
    uint16_t sequence;
    uint16_t command;
    uint16_t payload_length;
    uint16_t session_id;
    uint8_t  source;
    uint8_t  destination;
    uint32_t timestamp_ms;
} VaxpHeader;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpResponseHeader {
    int16_t  status;
    uint16_t detail;
} VaxpResponseHeader;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpAckPayload {
    uint16_t ack_sequence;
    uint16_t ack_command;
    int16_t  status;
    uint16_t detail;
} VaxpAckPayload;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpTlvHeader {
    uint16_t type;
    uint16_t length;
} VaxpTlvHeader;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpFragmentHeader {
    uint32_t message_id;
    uint16_t fragment_index;
    uint16_t fragment_count;
    uint32_t total_length;
    uint32_t offset;
} VaxpFragmentHeader;
VAXP_PACKED_END

VAXP_PACKED_BEGIN
typedef struct VAXP_PACKED VaxpUuid {
    uint8_t data[16];
} VaxpUuid;
VAXP_PACKED_END

#define VAXP_TLV_FLAG_CRITICAL 0x8000u
#define VAXP_TLV_TYPE_MASK     0x7FFFu
#define VAXP_TLV_IS_CRITICAL(type_) ((((uint16_t)(type_)) & VAXP_TLV_FLAG_CRITICAL) != 0u)
#define VAXP_TLV_BASE_TYPE(type_)    (((uint16_t)(type_)) & VAXP_TLV_TYPE_MASK)

VAXP_STATIC_ASSERT(sizeof(VaxpHeader) == 20u, "VaxpHeader wire ABI must be 20 bytes");
VAXP_STATIC_ASSERT(sizeof(VaxpResponseHeader) == 4u, "VaxpResponseHeader wire ABI must be 4 bytes");
VAXP_STATIC_ASSERT(sizeof(VaxpAckPayload) == 8u, "VaxpAckPayload wire ABI must be 8 bytes");
VAXP_STATIC_ASSERT(sizeof(VaxpTlvHeader) == 4u, "VaxpTlvHeader wire ABI must be 4 bytes");
VAXP_STATIC_ASSERT(sizeof(VaxpFragmentHeader) == 16u, "VaxpFragmentHeader wire ABI must be 16 bytes");
VAXP_STATIC_ASSERT(sizeof(VaxpUuid) == 16u, "VaxpUuid wire ABI must be 16 bytes");

/* Little-endian helpers. These are explicit to keep the wire ABI independent of host endianness. */
static inline uint16_t vaxp_read_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t vaxp_read_le32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static inline uint64_t vaxp_read_le64(const uint8_t *p)
{
    return (uint64_t)vaxp_read_le32(p)
         | ((uint64_t)vaxp_read_le32(p + 4) << 32);
}

static inline void vaxp_write_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static inline void vaxp_write_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static inline void vaxp_write_le64(uint8_t *p, uint64_t v)
{
    vaxp_write_le32(p, (uint32_t)(v & UINT64_C(0xFFFFFFFF)));
    vaxp_write_le32(p + 4, (uint32_t)(v >> 32));
}

static inline size_t vaxp_frame_size(uint8_t header_length, uint16_t payload_length)
{
    return (size_t)header_length + (size_t)payload_length + (size_t)VAXP_CRC_SIZE;
}

/* Implemented by the protocol library. */
uint16_t vaxp_crc16_ccitt_false(const uint8_t *data, size_t length);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* VAXP_PROTOCOL_H */
