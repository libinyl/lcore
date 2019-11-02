#ifndef _IOBUF_H_
#define _IOBUF_H_

#include <cstddef>
#include <cstdint>
#include "ucoredef.h"

UCORE_NAMESPACE_START


class iobuf
{
private:
	void* io_base;     // the base addr of buffer (used for Rd/Wr)
	off_t io_offset;   // current Rd/Wr position in buffer, will have been incremented by the amount transferred
	size_t io_len;     // the length of buffer  (used for Rd/Wr)
	size_t io_resid;   // current resident length need to Rd/Wr, will have been decremented by the amount transferred.

	iobuf& operator=(const iobuf& other) = default;
	iobuf(const iobuf& other) = default;
public:
	iobuf(void* base, size_t len, off_t offset)
		: io_base(base), io_offset(offset), io_len(len), io_resid(len)
	{}
	~iobuf() = default;

	size_t iobuf_used() const
	{
		return io_len - io_resid;
	}

	int iobuf_move(void* data, size_t len, bool m2b, size_t* copiedp)
	{/* Ê¡ÂÔ */
	};
	int iobuf_move_zeros(size_t len, size_t* copiedp)
	{ /* Ê¡ÂÔ */
	};
	void iobuf_skip(size_t n)
	{/* Ê¡ÂÔ */
	}
};

UCORE_NAMESPACE_END

#endif