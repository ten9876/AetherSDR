/*---------------------------------------------------------------------------*\

  rade_api.h

  Library of API functions that implement the Radio Autoencoder API.

\*---------------------------------------------------------------------------*/

/*
  Copyright (C) 2024 David Rowe

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

  - Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

  - Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef __RADE_API__
#define __RADE_API__

#ifdef _WIN32
#include <stddef.h>
#else
#include <sys/types.h>
#endif

#if IS_BUILDING_RADE_API
#if defined(_WIN32) && defined(RADE_BUILD_DLL)
#define RADE_EXPORT __declspec(dllexport) __stdcall
#elif defined(_WIN32)
#define RADE_EXPORT  /* static lib — no dllexport/stdcall */
#else
#define RADE_EXPORT __attribute__((visibility("default")))
#endif
#else
#if defined(_WIN32) && defined(RADE_BUILD_DLL)
#define RADE_EXPORT __declspec(dllimport) __stdcall
#elif defined(_WIN32)
#define RADE_EXPORT
#else
#define RADE_EXPORT
#endif
#endif // IS_BUILDING_RADE_API

// This declares a single-precision (float) complex number

#ifndef __RADE_COMP__
#define __RADE_COMP__
typedef struct {
  float real;
  float imag;
} RADE_COMP;
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Sample rates used
#define RADE_MODEM_SAMPLE_RATE 8000           // modem waveform sample rate
#define RADE_SPEECH_SAMPLE_RATE 16000         // speech sample rate

// init rade_open() flags
#define RADE_USE_C_ENCODER 0x1
#define RADE_USE_C_DECODER 0x2
#define RADE_FOFF_TEST     0x4                // test mode used only by developers
#define RADE_VERBOSE_0     0x8                // reduce verbosity to "quiet"

// Must be called BEFORE any other RADE functions as this
// initializes internal library state.
RADE_EXPORT void rade_initialize(void);

// Should be called when done with RADE.
RADE_EXPORT void rade_finalize(void);

// note single context only in this version, one context has one Tx, and one Rx
RADE_EXPORT struct rade *rade_open(char model_file[], int flags);
RADE_EXPORT void rade_close(struct rade *r);

// Allows API users to determine if the API has changed
RADE_EXPORT int rade_version(void);

// helpers to set up arrays
RADE_EXPORT int rade_n_tx_out(struct rade *r);
RADE_EXPORT int rade_n_tx_eoo_out(struct rade *r);
RADE_EXPORT int rade_nin_max(struct rade *r);
RADE_EXPORT int rade_n_features_in_out(struct rade *r);
RADE_EXPORT int rade_n_eoo_bits(struct rade *r);

// note vocoder is not encapsulated in API in this version
// returns number of RADE_COMP samples written to tx_out[]
RADE_EXPORT int rade_tx(struct rade *r, RADE_COMP tx_out[], float features_in[]);

// Set the rade_n_eoo_bits() bits to be sent in the EOO frame, which are
// in +/- 1 float form (note NOT 1 or 0)
RADE_EXPORT void rade_tx_set_eoo_bits(struct rade *r, float eoo_bits[]);

// call this for the final frame at the end of over
// returns the number of RADE_COMP samples written to tx_eoo_out[] 
RADE_EXPORT int rade_tx_eoo(struct rade *r, RADE_COMP tx_eoo_out[]);

// call me before each call to rade_rx(), provide nin samples to rx_in[]
RADE_EXPORT int rade_nin(struct rade *r);

// returns non-zero if features_out[] contains valid output. The number
// returned is the number of samples written to features_out[].  If the
// has_eoo_out is set, eoo_out[] contains End of Over soft decision bits
// from QPSK symbols in ..IQIQI... order
RADE_EXPORT int rade_rx(struct rade *r, float features_out[], int *has_eoo_out, float eoo_out[], RADE_COMP rx_in[]);

// returns non-zero if Rx is currently in sync
RADE_EXPORT int rade_sync(struct rade *r);

// returns the current frequency offset of the Rx signal ( when rade_sync()!=0 )
RADE_EXPORT float rade_freq_offset(struct rade *r);

// returns the current SNR estimate (in dB) of the Rx signal ( when rade_sync()!=0 )
RADE_EXPORT int rade_snrdB_3k_est(struct rade *r);

// test mode: disable unsync after this many seconds (0 = disabled)
RADE_EXPORT void rade_set_disable_unsync(struct rade *r, float seconds);

#ifdef __cplusplus
}
#endif

#endif  //__RADE_API__
