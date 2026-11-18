/*
 * The lobby's map transfer (phase N, stage N3; atlas P13 and P14 "Map transfer", fd.cpp and fd_user.cpp): the host
 * offers the current map (or replay) to every player that joins, and a player without it receives it in chunks from any
 * seat that already has it. Seven packet types travel as the chat room's direct message 'O' (kp_netgame.h); every byte
 * is the original's, so a Koprulu seat sends to and receives from StarCraft 1.17.
 *
 * A pure state machine: packets in (kp_transfer_dispatch), packets out through the I/O's send; the files through the
 * I/O's read, find and save. Nothing is timed: every packet answers another one.
 *
 * Where it differs from the original, only for inputs the original mishandles:
 *   - an offered file above KP_TRANSFER_MAX_SIZE, and an offer beyond KP_TRANSFER_MAX_ACTIVE transfers, is refused
 *     (the original allocates any DWORD's size, for every offer);
 *   - an offered name must end inside the packet (the original reads up to 32 bytes past a short one);
 *   - an offered name is refused for a drive prefix and a ':' anywhere as well (IsInvalidFileName 0x408300 skips a
 *     drive prefix, so "c:x.scm" would pass it), and the original's check that some search directory leaves room for
 *     the name (its MAX_PATH buffer) is not made: the I/O's save joins the download directory itself;
 *   - at most KP_TRANSFER_OFFERS files offered by this seat (the original's 255 file ids; the game offers one);
 *   - a local copy whose size is not the offer's fails the transfer: an offer from this seat's own Storm ID naming
 *     one of its offered files with another size (the original copies the whole file into a buffer of the offer's);
 *   - an upload request whose receiver is past Storm ID 7 is refused (the original sends the chunks to that ID, which
 *     Storm refuses: no StarCraft game has a player there);
 *   - a chunk acknowledgement or a stop from a seat other than the upload's receiver is ignored (the original asks only
 *     for the transfer, so any seat could make this one send chunks or end its upload).
 *
 * What the protocol cannot give, and Koprulu keeps for the sake of 1.17: a download is vouched for by the offer's
 * CRC-32 alone, and its data packets by no sender (the receiver is not told which seat the origin redirected to
 * upload), so a seat of the same game can inject a file of the offered size and CRC-32 (a CRC-32 is easy to match).
 * What bounds it: the size, the name and the folder checks above, and the map's own loader, which reads untrusted
 * files within bounds (the `chk` fuzz target); the host can kick the seat.
 */
#ifndef KP_TRANSFER_H
#define KP_TRANSFER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KP_TRANSFER_NAME 32               /* a file's name, its NUL included */
#define KP_TRANSFER_PATH 260              /* MAX_PATH */
#define KP_TRANSFER_CHUNK 128             /* SendFileChunk 0x441720's bound */
#define KP_TRANSFER_START_CHUNKS 8        /* an upload starts with eight chunks of 8 bytes */
#define KP_TRANSFER_START_BYTES 8
#define KP_TRANSFER_MAX_SIZE (16u << 20)  /* Koprulu's bound of an offered file */
#define KP_TRANSFER_MAX_ACTIVE 16         /* Koprulu's bound of the active transfers */
#define KP_TRANSFER_OFFERS 8              /* Koprulu's bound of this seat's offered files */
#define KP_TRANSFER_PEERS 8               /* one peer node a Storm ID 0..7 */

/* The packets' type bytes */
enum { KP_TRANSFER_STATUS, KP_TRANSFER_OFFER, KP_TRANSFER_UPLOAD, KP_TRANSFER_CANCEL, KP_TRANSFER_DATA,
	KP_TRANSFER_ACK, KP_TRANSFER_STOP };
/* An active transfer's state bits (+0x34): exists, uploading, receiving; the exact value 8 is a failure */
#define KP_TRANSFER_EXISTS 1u
#define KP_TRANSFER_UPLOADING 2u
#define KP_TRANSFER_RECEIVING 4u
#define KP_TRANSFER_FAILED 8u
/* A peer node's state before its first status */
#define KP_TRANSFER_OFFER_SENT 0x10u

typedef struct {
	/* SendMapDataChunk 0x471800: a packet to a Storm ID */
	void (*send)(void *ud, int storm, const uint8_t *packet, size_t size);
	/* LoadWholeMpqFileExOptional 0x444990 of a disk file: its bytes (malloc'd, the caller frees) and size, NULL for a
	 * missing or empty one */
	uint8_t *(*read)(void *ud, const char *path, size_t *size);
	/* FindLocalCopy 0x441080 for another seat's offer: a file of this name (case-insensitive) with this size and
	 * CRC-32 (kp_transfer_crc) in the user's maps folder (its download folder included) or the installation's, its
	 * path into out; 1, or 0 for none */
	int (*find)(void *ud, const char *name, uint32_t size, uint32_t crc, char *out, size_t out_size);
	/* SaveDownloadedFile 0x4418c0: a finished download written as `name` (checked: kp_transfer_name_valid) into the
	 * download folder (<user>maps\download\), overwriting a file of that name; its path into out; 1, or 0 when the
	 * folder or the file could not be written */
	int (*save)(void *ud, const char *name, const uint8_t *data, size_t size, char *out, size_t out_size);
	void *ud;
} KpTransferIo;

/* A player this seat offered a file to (16 bytes in the original) */
typedef struct {
	uint8_t storm, state;
	uint32_t count; /* the bytes the peer has, from its last status */
} KpTransferPeer;

/* A file this seat offers (0x128 bytes): its peers from the most recently added (the original's list head) */
typedef struct {
	uint8_t id;
	uint32_t pending; /* bit n: Storm ID n joined Storm, its join request has not run yet */
	uint32_t size, crc;
	char path[KP_TRANSFER_PATH];
	int peer_count;
	KpTransferPeer peers[KP_TRANSFER_PEERS];
} KpTransferOffer;

/* A file this seat receives or holds (0x48 bytes) */
typedef struct {
	uint8_t id, origin;
	uint32_t size, crc;
	char name[KP_TRANSFER_NAME];
	uint8_t state;
	uint32_t next;   /* +0x38: the next offset to send */
	uint8_t target;  /* +0x3c: the upload's receiver */
	uint8_t uploader; /* +0x3d: the seat the data comes from */
	uint32_t received;
	uint8_t *buffer; /* size bytes */
} KpTransferActive;

typedef struct {
	KpTransferIo io;
	int open;              /* BeginFileTransferSession ran (the send callback 0x665f88 is set) */
	uint8_t local;         /* 0x665f80 */
	uint8_t next_id;       /* 0x665f81 */
	uint32_t ack_chunk;    /* 0x665f84: the packet capacity - 9 */
	/* the offered files and the active transfers, each from the original's list head (the most recent first) */
	int offer_count, active_count;
	KpTransferOffer *offers[KP_TRANSFER_OFFERS];
	KpTransferActive *active[KP_TRANSFER_MAX_ACTIVE];
	/* SetCurrentMapPath 0x46b8d0 from a local copy or a saved download: the map the lobby plays (0x52701c) */
	char map_path[KP_TRANSFER_PATH];
	int map_ready; /* 0x666564 */
	uint32_t refused;      /* packets refused by Koprulu's bounds (the header's list), for the tests' and fuzzers' view */
} KpTransfer;

/* ComputeBlockChecksum 0x413ab0: CRC-32 with the initial value 0xffffffff and no final inversion. */
uint32_t kp_transfer_crc(const uint8_t *data, size_t size);
/* IsTransferableExtension 0x441020: the text after the last '.' is scx, scm or rep (ASCII case-insensitive). */
int kp_transfer_extension_ok(const char *name);
/* IsInvalidFileName 0x408300 (allowPath 0) inverted, with the header's stricter drive rule: 1 when a received name may
 * be saved: it ends within `limit` bytes, holds no byte 1..31, none of *\/:><|&+^?" and no DOS device name before
 * its first '.'. */
int kp_transfer_name_valid(const char *name, size_t limit);

/* BeginFileTransferSession 0x440750: every offer and transfer freed, the session's local Storm ID and packet capacity
 * (the provider's maximum message - 4). */
void kp_transfer_begin(KpTransfer *t, const KpTransferIo *io, int local, uint32_t capacity);
/* ResetFileTransfers 0x440730 and the session closed (nothing sent). */
void kp_transfer_reset(KpTransfer *t);
/* OfferLocalFile 0x440ab0: an offer of the local file at path (its name after the last '\\', or the host's '/');
 * NULL when its name is 32 bytes or longer, its extension not transferable, the file missing or empty, or no offer is
 * left. */
KpTransferOffer *kp_transfer_offer(KpTransfer *t, const char *path);
/* CancelOfferedFile 0x440b60: a cancel to each peer, the offer freed. */
void kp_transfer_cancel(KpTransfer *t, KpTransferOffer *o);
/* AddPeerToOfferedFile 0x4409b0: the peer's node and the offer sent to it (nothing for a known peer). */
void kp_transfer_add_peer(KpTransfer *t, KpTransferOffer *o, int storm);
/* RemovePeerFromOfferedFile 0x440be0. */
void kp_transfer_remove_peer(KpTransferOffer *o, int storm);
/* SetOfferedFileFlags 0x4409a0 and ClearOfferedFileFlags 0x440bd0. */
void kp_transfer_set_pending(KpTransferOffer *o, uint32_t bits);
void kp_transfer_clear_pending(KpTransferOffer *o, uint32_t bits);
/* AllPeersHaveFile 0x440ba0. */
int kp_transfer_all_have(const KpTransferOffer *o);
/* GetDownloadProgress 0x440ee0: the percentage over the active transfers into *pct; 0 when one failed. */
int kp_transfer_progress(const KpTransfer *t, uint8_t *pct);
/* AbortTransfersOfPeer 0x440f40. */
void kp_transfer_abort_peer(KpTransfer *t, int storm);
/* DispatchFileTransferMessage 0x4406c0: a packet from a Storm ID; the handler's 1, or 0. */
int kp_transfer_dispatch(KpTransfer *t, const uint8_t *packet, size_t size, int sender);

#ifdef __cplusplus
}
#endif

#endif
