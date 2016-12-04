/*
 * Copyright (c) 2015, 2016 Jerry Ryle and Jonathan G. Underwood
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <py3c.h>
#include <py3c/capsulethunk.h>

#include <stdlib.h>
#include <lz4frame.h>

#ifndef Py_UNUSED		/* This is already defined for Python 3.4 onwards */
#ifdef __GNUC__
#define Py_UNUSED(name) _unused_ ## name __attribute__((unused))
#else
#define Py_UNUSED(name) _unused_ ## name
#endif
#endif


#if defined(_WIN32) && defined(_MSC_VER)
#define inline __inline
#if _MSC_VER >= 1600
#include <stdint.h>
#else /* _MSC_VER >= 1600 */
typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
#endif /* _MSC_VER >= 1600 */
#endif

#if defined(__SUNPRO_C) || defined(__hpux) || defined(_AIX)
#define inline
#endif

struct compression_context
{
  LZ4F_compressionContext_t compression_context;
  LZ4F_preferences_t preferences;
};

/*****************************
* create_compression_context *
******************************/
PyDoc_STRVAR(create_compression_context__doc,
             "create_compression_context()\n\n"                         \
             "Creates a Compression Context object, which will be used in all\n" \
             "compression operations.\n\n"                              \
             "Returns:\n"                                               \
             "    cCtx: A compression context\n"
            );

static PyObject *
create_compression_context (PyObject * Py_UNUSED (self),
                            PyObject * Py_UNUSED (args),
                            PyObject * Py_UNUSED (keywds))
{
  struct compression_context * context;
  LZ4F_errorCode_t result;

  context =
    (struct compression_context *)
    PyMem_Malloc (sizeof (struct compression_context));

  if (!context)
    {
      return PyErr_NoMemory ();
    }

  memset (context, 0, sizeof (*context));

  result =
    LZ4F_createCompressionContext (&context->compression_context,
                                   LZ4F_VERSION);

  if (LZ4F_isError (result))
    {
      PyErr_Format (PyExc_RuntimeError,
                    "LZ4F_createCompressionContext failed with code: %s",
                    LZ4F_getErrorName (result));
      return NULL;
    }

  return PyCapsule_New (context, NULL, NULL);
}

/****************************
 * free_compression_context *
 ****************************/
PyDoc_STRVAR(free_compression_context__doc,
             "free_compression_context(context)\n\n"                                \
             "Releases the resources held by a compression context previously\n" \
             "created with create_compression_context.\n\n"             \
             "Args:\n"                                                  \
             "    context (cCtx): Compression context.\n"
             );

static PyObject *
free_compression_context (PyObject * Py_UNUSED (self), PyObject * args,
                          PyObject * keywds)
{
  PyObject *py_context = NULL;
  static char *kwlist[] = { "context", NULL };
  struct compression_context *context;
  LZ4F_errorCode_t result;

  if (!PyArg_ParseTupleAndKeywords (args, keywds, "O", kwlist, &py_context))
    {
      return NULL;
    }

  context =
    (struct compression_context *) PyCapsule_GetPointer (py_context, NULL);
  if (!context)
    {
      PyErr_Format (PyExc_ValueError, "No compression context supplied");
      return NULL;
    }

  result =
    LZ4F_freeCompressionContext (context->compression_context);
  if (LZ4F_isError (result))
    {
      PyErr_Format (PyExc_RuntimeError,
                    "LZ4F_freeCompressionContext failed with code: %s",
                    LZ4F_getErrorName (result));
      return NULL;
    }
  PyMem_Free (context);

  Py_RETURN_NONE;
}

/******************
 * compress_frame *
 ******************/
#define __COMPRESS_KWARGS_DOCSTRING \
  "    block_size (int): Sepcifies the maximum blocksize to use.\n"     \
  "        Options:\n\n"                                                \
  "        - BLOCKSIZE_DEFAULT or 0: the lz4 library default\n"         \
  "        - BLOCKSIZE_MAX64KB or 4: 64 kB\n"                           \
  "        - BLOCKSIZE_MAX256KB or 5: 256 kB\n"                         \
  "        - BLOCKSIZE_MAX1MB or 6: 1 MB\n"                             \
  "        - BLOCKSIZE_MAX1MB or 7: 4 MB\n\n"                           \
  "        If unspecified, will default to BLOCKSIZE_DEFAULT.\n"        \
  "    block_mode (int): Specifies whether to use block-linked\n"       \
  "        compression. Options:\n\n"                                   \
  "        - BLOCKMODE_INDEPENDENT or 0: disable linked mode\n"         \
  "        - BLOCKMODE_LINKED or 1: linked mode\n\n"                    \
  "        The default is BLOCKMODE_INDEPENDENT.\n"                     \
  "    compression_level (int): Specifies the level of compression used.\n" \
  "        Values between 0-16 are valid, with 0 (default) being the\n" \
  "        lowest compression, and 16 the highest. Values above 16 will\n" \
  "        be treated as 16. Values betwee 3-6 are recommended.\n"      \
  "        The following module constants are provided as a convenience:\n\n" \
  "        - COMPRESSIONLEVEL_MIN: Minimum compression (0, the default)\n" \
  "        - COMPRESSIONLEVEL_MINHC: Minimum high-compression mode (3)\n" \
  "        - COMPRESSIONLEVEL_MAX: Maximum compression (16)\n\n"        \
  "    content_checksum (int): Specifies whether to enable checksumming of\n" \
  "        the payload content. Options:\n\n"                           \
  "        - CONTENTCHECKSUM_DISABLED or 0: disables checksumming\n"    \
  "        - CONTENTCHECKSUM_ENABLED or 1: enables checksumming\n\n"    \
  "        The default is CONTENTCHECKSUM_DISABLED.\n"                  \
  "    frame_type (int): Specifies whether user data can be injected between\n" \
  "        frames. Options:\n\n"                                        \
  "        - FRAMETYPE_FRAME or 0: disables user data injection\n"      \
  "        - FRAMETYPE_SKIPPABLEFRAME or 1: enables user data injection\n\n" \
  "        The default is FRAMETYPE_FRAME.\n"                           \
  "    source_size (int): This optionally specifies  the uncompressed size\n" \
  "        of the full frame content. This arument is optional, but can be\n" \

PyDoc_STRVAR(compress_frame__doc,
             "compress_frame(source)\n\n"                               \
             "Accepts a string, and compresses the string in one go, returning the\n" \
             "compressed string as a string of bytes. The compressed string includes\n" \
             "a header and endmark and so is suitable for writing to a file.\n\n" \
             "Args:\n"                                                  \
             "    source (str): String to compress\n\n"                 \
             "Keyword Args:\n"                                          \
             __COMPRESS_KWARGS_DOCSTRING                                \
             "\n"                                                       \
             "Returns:\n"                                               \
             "    str: Compressed data as a string\n"
             );

static PyObject *
compress_frame (PyObject * Py_UNUSED (self), PyObject * args,
                PyObject * keywds)
{
  const char *source;
  int source_size;
  LZ4F_preferences_t preferences;
  size_t compressed_bound;
  Py_ssize_t dest_size;
  PyObject *py_dest;
  char *dest;

  static char *kwlist[] = { "source",
                            "compression_level",
                            "block_size",
                            "content_checksum",
                            "block_mode",
                            "frame_type",
                            NULL
                          };


  memset (&preferences, 0, sizeof (preferences));

  if (!PyArg_ParseTupleAndKeywords (args, keywds, "s#|iiiii", kwlist,
                                    &source, &source_size,
                                    &preferences.compressionLevel,
                                    &preferences.frameInfo.blockSizeID,
                                    &preferences.
                                    frameInfo.contentChecksumFlag,
                                    &preferences.frameInfo.blockMode,
                                    &preferences.frameInfo.frameType))
    {
      return NULL;
    }
  preferences.autoFlush = 0;
  preferences.frameInfo.contentSize = source_size;

  compressed_bound =
    LZ4F_compressFrameBound (source_size, &preferences);

  if (compressed_bound > PY_SSIZE_T_MAX)
    {
      PyErr_Format (PyExc_ValueError,
                    "input data could require %zu bytes, which is larger than the maximum supported size of %zd bytes",
                    compressed_bound, PY_SSIZE_T_MAX);
      return NULL;
    }

  dest_size = (Py_ssize_t) compressed_bound;

  py_dest = PyBytes_FromStringAndSize (NULL, dest_size);
  if (py_dest == NULL)
    {
      return NULL;
    }

  dest = PyBytes_AS_STRING (py_dest);
  if (source_size > 0)
    {
      size_t compressed_size =
        LZ4F_compressFrame (dest, dest_size, source, source_size,
                            &preferences);
      if (LZ4F_isError (compressed_size))
        {
          Py_DECREF (py_dest);
          PyErr_Format (PyExc_RuntimeError,
                        "LZ4F_compressFrame failed with code: %s",
                        LZ4F_getErrorName (compressed_size));
          return NULL;
        }
      /* The actual compressed size might be less than we allocated
         (we allocated using a worst case guess). If the actual size is
         less than 75% of what we allocated, then it's worth performing an
         expensive resize operation to reclaim some space. */
      if ((Py_ssize_t) compressed_size < (dest_size / 4) * 3)
        {
          _PyBytes_Resize (&py_dest, (Py_ssize_t) compressed_size);
        }
      else
        {
          Py_SIZE (py_dest) = (Py_ssize_t) compressed_size;
        }
    }
  return py_dest;
}

/******************
 * compress_begin *
 ******************/
PyDoc_STRVAR(compress_begin__doc,
             "compressBegin(cCtx)\n\n"                                  \
             "Creates a frame header from a compression context.\n\n"   \
             "Args:\n"                                                  \
             "    context (cCtx): A compression context.\n\n"           \
             "Keyword Args:\n"                                          \
             __COMPRESS_KWARGS_DOCSTRING                                \
             "    auto_flush (int): Enable () or disable (0) autoFlush in LZ4\n" \
             "         When autoFlush is disabled, the LZ4 library may buffer data\n" \
             "         until a block is full\n\n"                       \
             "Returns:\n"                                               \
             "    str (str): Frame header.\n"
             );

#undef __COMPRESS_KWARGS_DOCSTRING

static PyObject *
compress_begin (PyObject * Py_UNUSED (self), PyObject * args,
                PyObject * keywds)
{
  PyObject *py_context = NULL;
  unsigned long source_size = 0;
  LZ4F_preferences_t preferences;
  static char *kwlist[] = { "context",
                            "source_size",
                            "compression_level",
                            "block_size",
                            "content_checksum",
                            "block_mode",
                            "frame_type",
                            "auto_flush",
                            NULL
                          };

  memset (&preferences, 0, sizeof (preferences));

  /* Default to having autoFlush enabled unless specified otherwise via keyword
     argument */
  preferences.autoFlush = 1;

  if (!PyArg_ParseTupleAndKeywords (args, keywds, "O|kiiiiii", kwlist,
                                    &py_context,
                                    &source_size,
                                    &preferences.compressionLevel,
                                    &preferences.frameInfo.blockSizeID,
                                    &preferences.frameInfo.contentChecksumFlag,
                                    &preferences.frameInfo.blockMode,
                                    &preferences.frameInfo.frameType,
                                    &preferences.autoFlush
                                    ))
    {
      return NULL;
    }

  preferences.frameInfo.contentSize = source_size;

  struct compression_context *context =
    (struct compression_context *) PyCapsule_GetPointer (py_context, NULL);

  if (!context || !context->compression_context)
    {
      PyErr_Format (PyExc_ValueError, "No compression context supplied");
      return NULL;
    }

  context->preferences = preferences;

  /* Only needs to be large enough for a header, which is 15 bytes.
   * Unfortunately, the lz4 library doesn't provide a #define for this.
   * We over-allocate to allow for larger headers in the future. */
  char destination_buffer[64];

  size_t result = LZ4F_compressBegin (context->compression_context,
                                      destination_buffer,
                                      sizeof (destination_buffer),
                                      &context->preferences);
  if (LZ4F_isError (result))
    {
      PyErr_Format (PyExc_RuntimeError,
                    "LZ4F_compressBegin failed with code: %s",
                    LZ4F_getErrorName (result));
      return NULL;
    }

  return PyBytes_FromStringAndSize (destination_buffer, result);
}

/*******************
 * compress_update *
 *******************/
PyDoc_STRVAR(compress_update__doc,
             "compress_update(context, source)\n\n" \
             "Compresses blocks of data and returns the compressed data in a string of bytes.\n" \
             "Args:\n"                                                  \
             "    context (cCtx): compression context\n"                \
             "    source (str): data to compress\n\n"                   \
             "Returns:\n"                                               \
             "    str: Compressed data as a string\n\n"                 \
             "Notes:\n"                                               \
             "    If autoFlush is disabled (auto_flush=0 when calling compress_begin)\n" \
             "    this function may return an empty string if LZ4 decides to buffer.\n" \
             "    the input.\n"
             );

static PyObject *
compress_update (PyObject * Py_UNUSED (self), PyObject * args,
                 PyObject * keywds)
{
  PyObject *py_context = NULL;
  const char *source = NULL;
  unsigned long source_size = 0;

  static char *kwlist[] = { "context", "source", NULL };

  if (!PyArg_ParseTupleAndKeywords (args, keywds, "Os#", kwlist,
                                    &py_context, &source, &source_size))
    {
      return NULL;
    }

  struct compression_context *context =
    (struct compression_context *) PyCapsule_GetPointer (py_context, NULL);
  if (!context || !context->compression_context)
    {
      PyErr_Format (PyExc_ValueError, "No compression context supplied");
      return NULL;
    }

  /* If autoFlush is enabled, then the destination buffer only needs to be as
     big as LZ4F_compressFrameBound specifies for this source size. However, if
     autoFlush is disabled, previous calls may have resulted in buffered data,
     and so we need instead to use LZ4F_compressBound to find the size required
     for the destination buffer. This means that with autoFlush disabled we may
     frequently allocate more memory than needed. */
  size_t compressed_bound;
  if (context->preferences.autoFlush == 1)
    {
      compressed_bound =
        LZ4F_compressFrameBound (source_size, &context->preferences);
    }
  else
    {
      compressed_bound =
        LZ4F_compressBound (source_size, &context->preferences);
    }

  if (compressed_bound > PY_SSIZE_T_MAX)
    {
      PyErr_Format (PyExc_ValueError,
                    "input data could require %zu bytes, which is larger than the maximum supported size of %zd bytes",
                    compressed_bound, PY_SSIZE_T_MAX);
      return NULL;
    }

  char *destination_buffer = (char *) PyMem_Malloc (compressed_bound);
  if (!destination_buffer)
    {
      return PyErr_NoMemory ();
    }

  LZ4F_compressOptions_t compress_options;
  compress_options.stableSrc = 0;

  size_t result =
    LZ4F_compressUpdate (context->compression_context, destination_buffer,
                         compressed_bound, source, source_size,
                         &compress_options);
  if (LZ4F_isError (result))
    {
      PyMem_Free (destination_buffer);
      PyErr_Format (PyExc_RuntimeError,
                    "LZ4F_compressUpdate failed with code: %s",
                    LZ4F_getErrorName (result));
      return NULL;
    }
  PyObject *bytes = PyBytes_FromStringAndSize (destination_buffer, result);
  PyMem_Free (destination_buffer);

  return bytes;
}

/****************
 * compress_end *
 ****************/
PyDoc_STRVAR(compress_end__doc,
             "compress_end(context)\n\n" \
             "Flushes a compression context returning an endmark and optional checksum\n" \
             "as a string of bytes.\n" \
             "Args:\n"                                                  \
             "    context (cCtx): compression context\n"                \
             "Returns:\n"                                               \
             "    str: Remaining (buffered) compressed data, end mark and optional checksum as a string\n"
             );

static PyObject *
compress_end (PyObject * Py_UNUSED (self), PyObject * args, PyObject * keywds)
{
  PyObject *py_context = NULL;

  static char *kwlist[] = { "context", NULL };

  if (!PyArg_ParseTupleAndKeywords (args, keywds, "O", kwlist, &py_context))
    {
      return NULL;
    }

  struct compression_context *context =
    (struct compression_context *) PyCapsule_GetPointer (py_context, NULL);
  if (!context || !context->compression_context)
    {
      PyErr_Format (PyExc_ValueError, "No compression context supplied");
      return NULL;
    }

  LZ4F_compressOptions_t compress_options;
  compress_options.stableSrc = 0;

  /* Calling LZ4F_compressBound with srcSize equal to 1 returns a size
     sufficient to fit (i) any remaining buffered data (when autoFlush is
     disabled) and the footer size, which is either 4 or 8 bytes depending on
     whether checksums are enabled. https://github.com/lz4/lz4/issues/280 */
  size_t destination_size = LZ4F_compressBound(1, &(context->preferences));
  char destination_buffer[destination_size];

  size_t result =
    LZ4F_compressEnd (context->compression_context, destination_buffer,
                      sizeof (destination_buffer), &compress_options);
  if (LZ4F_isError (result))
    {
      PyErr_Format (PyExc_RuntimeError,
                    "LZ4F_compressBegin failed with code: %s",
                    LZ4F_getErrorName (result));
      return NULL;
    }

  return PyBytes_FromStringAndSize (destination_buffer, result);
}

/******************
 * get_frame_info *
 ******************/
PyDoc_STRVAR(get_frame_info__doc,
             "get_frame_info(frame)\n\n"                                \
             "Given a frame of compressed data, returns information about the frame.\n" \
             "Args:\n"                                                  \
             "    frame (str): LZ4 frame as a string\n"                \
             "Returns:\n"                                               \
             "    dict: Dictionary with keys blockSizeID, blockMode, contentChecksumFlag\n" \
             "         frameType and contentSize.\n"
             );

static PyObject *
get_frame_info (PyObject * Py_UNUSED (self), PyObject * args,
                PyObject * keywds)
{
  const char *source;
  int source_size;

  static char *kwlist[] = { "source", NULL };

  if (!PyArg_ParseTupleAndKeywords (args, keywds, "s#", kwlist,
                                    &source, &source_size))
    {
      return NULL;
    }

  LZ4F_decompressionContext_t context;
  size_t result = LZ4F_createDecompressionContext (&context, LZ4F_VERSION);
  if (LZ4F_isError (result))
    {
      PyErr_Format (PyExc_RuntimeError,
                    "LZ4F_createDecompressionContext failed with code: %s",
                    LZ4F_getErrorName (result));
      return NULL;
    }

  LZ4F_frameInfo_t frame_info;
  size_t source_size_copy = source_size;
  result =
    LZ4F_getFrameInfo (context, &frame_info, source, &source_size_copy);
  if (LZ4F_isError (result))
    {
      LZ4F_freeDecompressionContext (context);
      PyErr_Format (PyExc_RuntimeError,
                    "LZ4F_getFrameInfo failed with code: %s",
                    LZ4F_getErrorName (result));
      return NULL;
    }

  result = LZ4F_freeDecompressionContext (context);
  if (LZ4F_isError (result))
    {
      PyErr_Format (PyExc_RuntimeError,
                    "LZ4F_freeDecompressionContext failed with code: %s",
                    LZ4F_getErrorName (result));
      return NULL;
    }

  return Py_BuildValue ("{s:i,s:i,s:i,s:i,s:i}",
                        "blockSizeID", frame_info.blockSizeID,
                        "blockMode", frame_info.blockMode,
                        "contentChecksumFlag", frame_info.contentChecksumFlag,
                        "frameType", frame_info.frameType,
                        "contentSize", frame_info.contentSize);
}

/**************
 * decompress *
 **************/
PyDoc_STRVAR(decompress__doc,
             "decompress(source, uncompressed_size=0)\n\n"                                \
             "Decompressed a frame of data and returns it as a string of bytes.\n" \
             "Args:\n"                                                  \
             "    source (str): LZ4 frame as a string\n\n"              \
             "Returns:\n"                                               \
             "    str: Uncompressed data as a string.\n"
             );

static PyObject *
decompress (PyObject * Py_UNUSED (self), PyObject * args, PyObject * keywds)
{
  char const *source;
  int source_size;

  if (!PyArg_ParseTuple (args, "s#", &source, &source_size))
    {
      return NULL;
    }

  LZ4F_decompressionContext_t context;
  size_t result = LZ4F_createDecompressionContext (&context, LZ4F_VERSION);
  if (LZ4F_isError (result))
    {
      PyErr_Format (PyExc_RuntimeError,
                    "LZ4F_createDecompressionContext failed with code: %s",
                    LZ4F_getErrorName (result));
      return NULL;
    }

  LZ4F_frameInfo_t frame_info;
  size_t source_read = source_size;
  result =
    LZ4F_getFrameInfo (context, &frame_info, source, &source_read);
  if (LZ4F_isError (result))
    {
      LZ4F_freeDecompressionContext (context);
      PyErr_Format (PyExc_RuntimeError,
                    "LZ4F_getFrameInfo failed with code: %s",
                    LZ4F_getErrorName (result));
      return NULL;
    }
  /* Advance the source pointer past the header - the call to getFrameInfo above
     replaces the passed source_read value with the number of bytes
     read. Also reduce source_size accordingly. */
  source += source_read;
  source_size -= source_read;

  size_t destination_size;
  if (frame_info.contentSize == 0)
    {
      /* We'll allocate twice the source buffer size as the output size, and
         later increase it if needed. */
      destination_size = 2 * source_size;
    }
  else
    {
      destination_size = frame_info.contentSize;
    }

  char * destination_buffer = (char *) PyMem_Malloc (destination_size);
  if (!destination_buffer)
    {
      LZ4F_freeDecompressionContext (context);
      return PyErr_NoMemory ();
    }

  LZ4F_decompressOptions_t options;
  options.stableDst = 1;

  source_read = source_size;
  const void * source_cursor = source;
  const void * source_end = source + source_size;

  size_t destination_write = destination_size;
  void * destination_cursor = destination_buffer;
  size_t destination_written = 0;

  while (1)
    {
      size_t result = LZ4F_decompress (context,
                                       destination_cursor,
                                       &destination_write,
                                       source_cursor,
                                       &source_read,
                                       &options);

      if (LZ4F_isError (result))
        {
          PyErr_Format (PyExc_RuntimeError,
                        "LZ4F_decompress failed with code: %s",
                        LZ4F_getErrorName (result));
          LZ4F_freeDecompressionContext (context);
          PyMem_Free (destination_buffer);
          return NULL;
        }

      destination_written += destination_write;

      if (result == 0)
        {
          break;
        }

      if (destination_written == destination_size)
        {
          /* Destination_buffer is full, so need to expand it. We'll expand
             it by the approximate size needed from the return value - see
             LZ4 docs */
          destination_size += result;
          if (!PyMem_Realloc(destination_buffer, destination_size))
            {
              PyErr_SetString (PyExc_RuntimeError,
                               "Failed to increase destination buffer size");
              LZ4F_freeDecompressionContext (context);
              PyMem_Free (destination_buffer);
              return NULL;
            }
        }
      /* Data still remaining to be decompressed, so increment the source and
         destination cursor locations, and reset source_read and
         destination_write ready for the next iteration. Important to
         re-initialize destination_cursor here in this was (as opposed to simply
         incrementing it) so we're pointing to the realloc'd memory location. */
      destination_cursor = destination_buffer + destination_written;
      source_cursor += source_read;
      destination_write = destination_size - destination_written;
      source_read = source_end - source_cursor;
    }

  result = LZ4F_freeDecompressionContext (context);
  if (LZ4F_isError (result))
    {
      PyMem_Free (destination_buffer);
      PyErr_Format (PyExc_RuntimeError,
                    "LZ4F_freeDecompressionContext failed with code: %s",
                    LZ4F_getErrorName (result));
      return NULL;
    }

  PyObject *py_dest =
    PyBytes_FromStringAndSize (destination_buffer, destination_written);

  if (py_dest == NULL)
    {
      PyErr_SetString (PyExc_RuntimeError,
                       "Failed to create Python object from destination_buffer");
    }

  PyMem_Free (destination_buffer);
  return py_dest;
}

static PyMethodDef module_methods[] =
{
  {
    "create_compression_context", (PyCFunction) create_compression_context,
    METH_VARARGS | METH_KEYWORDS, create_compression_context__doc
  },
  {
    "free_compression_context", (PyCFunction) free_compression_context,
    METH_VARARGS | METH_KEYWORDS, free_compression_context__doc
  },
  {
    "compress_frame", (PyCFunction) compress_frame,
    METH_VARARGS | METH_KEYWORDS, compress_frame__doc
  },
  {
    "compress_begin", (PyCFunction) compress_begin,
    METH_VARARGS | METH_KEYWORDS, compress_begin__doc
  },
  {
    "compress_update", (PyCFunction) compress_update,
    METH_VARARGS | METH_KEYWORDS, compress_update__doc
  },
  {
    "compress_end", (PyCFunction) compress_end,
    METH_VARARGS | METH_KEYWORDS, compress_end__doc
  },
  {
    "get_frame_info", (PyCFunction) get_frame_info,
    METH_VARARGS | METH_KEYWORDS, get_frame_info__doc
  },
  {
    "decompress", (PyCFunction) decompress,
    METH_VARARGS | METH_KEYWORDS, decompress__doc
  },
  {NULL, NULL, 0, NULL}		/* Sentinel */
};

PyDoc_STRVAR(lz4frame__doc,
             "A Python wrapper for the LZ4 frame protocol"
             );

static struct PyModuleDef moduledef =
{
  PyModuleDef_HEAD_INIT,
  "_frame",
  lz4frame__doc,
  -1,
  module_methods
};

MODULE_INIT_FUNC (_frame)
{
  PyObject *module = PyModule_Create (&moduledef);

  if (module == NULL)
    return NULL;

  PyModule_AddIntConstant (module, "BLOCKSIZE_DEFAULT", LZ4F_default);
  PyModule_AddIntConstant (module, "BLOCKSIZE_MAX64KB", LZ4F_max64KB);
  PyModule_AddIntConstant (module, "BLOCKSIZE_MAX256KB", LZ4F_max256KB);
  PyModule_AddIntConstant (module, "BLOCKSIZE_MAX1MB", LZ4F_max1MB);
  PyModule_AddIntConstant (module, "BLOCKSIZE_MAX4MB", LZ4F_max4MB);

  PyModule_AddIntConstant (module, "BLOCKMODE_LINKED", LZ4F_blockLinked);
  PyModule_AddIntConstant (module, "BLOCKMODE_INDEPENDENT",
                           LZ4F_blockIndependent);

  PyModule_AddIntConstant (module, "CONTENTCHECKSUM_DISABLED",
                           LZ4F_noContentChecksum);
  PyModule_AddIntConstant (module, "CONTENTCHECKSUM_ENABLED",
                           LZ4F_contentChecksumEnabled);

  PyModule_AddIntConstant (module, "FRAMETYPE_FRAME", LZ4F_frame);
  PyModule_AddIntConstant (module, "FRAMETYPE_SKIPPABLEFRAME",
                           LZ4F_skippableFrame);

  PyModule_AddIntConstant (module, "COMPRESSIONLEVEL_MIN", 0);
  PyModule_AddIntConstant (module, "COMPRESSIONLEVEL_MINHC", 3);
  PyModule_AddIntConstant (module, "COMPRESSIONLEVEL_MAX", 16);

  return module;
}