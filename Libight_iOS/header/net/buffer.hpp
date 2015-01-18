/*-
 * This file is part of Libight <https://libight.github.io/>.
 *
 * Libight is free software. See AUTHORS and LICENSE for more
 * information on the copying conditions.
 */

#ifndef LIBIGHT_NET_BUFFER_HPP
# define LIBIGHT_NET_BUFFER_HPP

#include "common/utils.hpp"

#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include <sys/uio.h>

#include <functional>
#include <stdlib.h>
#include <string.h>
#include <stdexcept>
#include <vector>

/* Apparently not defined on MacOSX */
#ifndef IOV_MAX
# define IOV_MAX 32
#endif

// Helper class for IghtBuffer (see below)
class IghtIovec {
	evbuffer_iovec *iov = NULL;
	size_t count = 0;

    public:
	IghtIovec(size_t n = 0) {
		if (n < 1)
			return;
		if ((iov = (evbuffer_iovec *)calloc(n, sizeof (*iov))) == NULL)
			throw std::bad_alloc();
		count = n;
	}

	~IghtIovec(void) {
		ight_xfree(iov);
	}

	IghtIovec(IghtIovec&) = delete;
	IghtIovec& operator=(IghtIovec&) = delete;

	IghtIovec(IghtIovec&& other) {
		std::swap(iov, other.iov);
		std::swap(count, other.count);
	}
	IghtIovec& operator=(IghtIovec&& other) {
		std::swap(iov, other.iov);
		std::swap(count, other.count);
		return (*this);
	}

	evbuffer_iovec *operator[](int i) {
		if (iov == NULL)
			throw std::runtime_error("Iovec is null");
		/*
		 * It is safe to cast i to size_t, because we exclude
		 * the case in which i is negative first.
		 */
		if (i < 0 || (size_t) i >= count)
			throw std::runtime_error("Invalid index");
		return (&iov[i]);
	}

	evbuffer_iovec *get_base(void) {
		if (iov == NULL)
			throw std::runtime_error("Iovec is null");
		return (iov);
	}

	size_t get_length(void) {
		if (iov == NULL)
			throw std::runtime_error("Iovec is null");
		return (count);
	}
};

class IghtBuffer {
	evbuffer *evbuf = NULL;

    public:
	IghtBuffer(void) {
		if ((evbuf = evbuffer_new()) == NULL)
			throw std::bad_alloc();
	}

	IghtBuffer(IghtBuffer&) = delete;
	IghtBuffer& operator=(IghtBuffer&) = delete;

	/*
	 * Note: move semantic entails an allocation. We can do better
	 * by replacing the evbuffer with a wrapper object implementing
	 * a lazy allocation strategy. (Coming soon...)
	 */
	IghtBuffer(IghtBuffer&& other) {
		std::swap(evbuf, other.evbuf);
	}
	IghtBuffer& operator=(IghtBuffer&& other) {
		std::swap(evbuf, other.evbuf);
		return (*this);
	}

	~IghtBuffer(void) {
		evbuffer_free(evbuf);	/* Cannot be NULL */
	}

	/*
	 * I expect to read (write) from (into) the input (output)
	 * evbuffer of a certain bufferevent. It seems to me natural
	 * to use the insertion and extraction operators for that.
	 */

	IghtBuffer& operator<<(evbuffer *source) {
		if (source == NULL)
			throw std::runtime_error("source is NULL");
		if (evbuffer_add_buffer(evbuf, source) != 0)
			throw std::runtime_error("evbuffer_add_buffer failed");
		return (*this);
	}

	IghtBuffer& operator>>(evbuffer *dest) {
		if (dest == NULL)
			throw std::runtime_error("dest is NULL");
		if (evbuffer_add_buffer(dest, evbuf) != 0)
			throw std::runtime_error("evbuffer_add_buffer failed");
		return (*this);
	}

	size_t length(void) {
		return (evbuffer_get_length(evbuf));
	}

	/*
	 * The following is useful to feed a parser (e.g., the http-parser)
	 * with all (or part of) the content of `this`.
	 */
	void foreach(std::function<bool(evbuffer_iovec *)> fn) {
		auto required_size = evbuffer_peek(evbuf, -1, NULL, NULL, 0);
		if (required_size < 0)
			throw std::runtime_error("unexpected error");
		if (required_size == 0)
			return;

		auto iov = IghtIovec(required_size);
		auto used = evbuffer_peek(evbuf, -1, NULL,
		    iov.get_base(), iov.get_length());
		if (used != required_size)
			throw std::runtime_error("unexpected error");

		for (auto i = 0; i < required_size && fn(iov[i]); ++i)
			/* nothing */ ;
	}

	/*
	 * Discard(), read(), readline() and readn() are the common operations
	 * that you need to implement a protocol (AFAICT).
	 */

	void discard(size_t count) {
		if (evbuffer_drain(evbuf, count) != 0)
			throw std::runtime_error("evbuffer_drain failed");
	}
	void discard(void) {
		discard(length());
	}

	/*
	 * This function is a template because sometimes we want to read
	 * text (in which case, std::basic_string<char> aka std::string is
	 * the appropriate type) and sometimes we want instead to read
	 * binary data (in which case std::basic_string<uint8_t> is more
	 * appropriate, because it is surprising to store binary data into
	 * an std::string, which is designed to hold text).
	 */
	template<typename T> std::basic_string<T> read(size_t upto) {
		size_t nbytes = 0;
		std::basic_string<T> out;

		if (sizeof (T) != 1)
			throw std::runtime_error("Wide chars not supported");

		foreach([&](evbuffer_iovec *iov) {
			if (upto < iov->iov_len)
				iov->iov_len = upto;

			out.append((const T *) iov->iov_base,
			    iov->iov_len);

			upto -= iov->iov_len;
			nbytes += iov->iov_len;
			return (upto > 0);
		});

		/*
		 * We do this after foreach() because we are not supposed
		 * to modify the underlying `evbuf` during foreach().
		 */
		discard(nbytes);

		return (out);
	}

	template<typename T> std::basic_string<T> read(void) {
		return (read<T>(length()));
	}

	/*
	 * The semantic of readn() is that we return a string only
	 * when we have exactly N bytes available.
	 */
	template<typename T> std::basic_string<T> readn(size_t n) {
		if (n > length())
			return (std::basic_string<T>());	/* Empty str */
		return (read<T>(n));
	}

	std::tuple<int, std::string> readline(size_t maxline) {

		size_t eol_length = 0;
		auto search_result = evbuffer_search_eol(evbuf, NULL,
		    &eol_length, EVBUFFER_EOL_CRLF);
		if (search_result.pos < 0) {
			if (length() > maxline)
				return (std::make_tuple(-1, ""));
			return (std::make_tuple(0, ""));
		}

		/*
		 * Promotion to size_t safe because eol_length is a small
		 * number and because we know that pos is non-negative.
		 */
		if (eol_length != 1 && eol_length != 2)
			throw std::runtime_error("unexpected error");
		auto len = (size_t) search_result.pos + eol_length;
		if (len > maxline)
			return (std::make_tuple(-2, ""));

		return (std::make_tuple(0, read<char>(len)));
	}

	/*
	 * Wrappers for write, including a handy wrapper for sending
	 * random bytes to the output stream.
	 */

	void write(std::string in) {
		write(in.c_str(), in.length());
	}

	IghtBuffer& operator<<(std::string in) {
		write(in);
		return (*this);
	}

	void write(std::vector<char> in) {
		write(in.data(), in.size());
	}

	IghtBuffer& operator<<(std::vector<char> in) {
		write(in);
		return (*this);
	}

	void write(const char *in) {
		if (in == NULL)
			throw std::runtime_error("in is NULL");
		write(in, strlen(in));
	}

	IghtBuffer& operator<<(const char *in) {
		write(in);
		return (*this);
	}

	void write(const void *buf, size_t count) {
		if (buf == NULL)
			throw std::runtime_error("buf is NULL");
		if (evbuffer_add(evbuf, buf, count) != 0)
			throw std::runtime_error("evbuffer_add failed");
	}

	void write_rand(size_t count) {
		if (count == 0)
			return;

		auto iov = IghtIovec(IOV_MAX);
		int i, n_extents;

		n_extents = evbuffer_reserve_space(evbuf, count,
		    iov.get_base(), iov.get_length());
		if (n_extents < 0)
			throw std::runtime_error(
			    "evbuffer_reserve_space failed");

		for (i = 0; i < n_extents && count > 0; i++) {
			/*
			 * Note: `iov[i]` throws if `i` is out of bounds.
			 */
			if (count < iov[i]->iov_len)
				iov[i]->iov_len = count;
			/*
			 * Implementation note: I'm not sure this is fast
			 * enough for Neubot needs; I'll check...
			 */
			evutil_secure_rng_get_bytes(iov[i]->iov_base,
			    iov[i]->iov_len);
			count -= iov[i]->iov_len;
		}

		/*
		 * Used to be an assert. I'd rather have a statement that
		 * throws on failure, so that it can be tested.
		 */
		if (!(count == 0 && i >= 0 && (size_t) i <= iov.get_length()))
			throw std::runtime_error("unexpected error");

		if (evbuffer_commit_space(evbuf, iov.get_base(), i) != 0)
			throw std::runtime_error(
			    "evbuffer_commit_space failed");
	}
};

#endif  /* LIBIGHT_NET_BUFFER_HPP */
