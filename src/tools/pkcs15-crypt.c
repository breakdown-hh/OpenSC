/*
 * pkcs15-crypt.c: Tool for cryptography operations with SmartCards
 *
 * Copyright (C) 2001  Juha Yrj�l� <juha.yrjola@iki.fi>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <opensc.h>
#include <opensc-pkcs15.h>

int opt_reader = 0, opt_pin = 0, quiet = 0;
int opt_debug = 0;
char * opt_pincode = NULL, * opt_key_id = NULL;
char * opt_input = NULL, * opt_output = NULL;
int opt_hash_type = SC_PKCS15_HASH_NONE;

#define OPT_SHA1 0x101

const struct option options[] = {
	{ "sign",		0, 0,		's' },
	{ "decipher",		0, 0,		'c' },
	{ "key",		1, 0,		'k' },
	{ "reader",		1, 0,		'r' },
	{ "input",		1, 0,		'i' },
	{ "output",		1, 0,		'o' },
	{ "sha-1",		0, 0,		OPT_SHA1 },
	{ "quiet",		0, 0,		'q' },
	{ "debug",		0, 0,		'd' },
	{ "pin",		1, 0,		'p' },
	{ "pin-id",		1, &opt_pin,	0   },
	{ 0, 0, 0, 0 }
};

const char *option_help[] = {
	"Performs digital signature operation",
	"Decipher operation",
	"Selects the private key ID to use",
	"Uses reader number <arg>",
	"Selects the input file to use",
	"Outputs to file <arg>",
	"Input file is a SHA-1 hash",
	"Quiet operation",
	"Debug output -- may be supplied several times",
	"Uses password (PIN) <arg>",
	"The auth ID of the PIN to use",
};

struct sc_context *ctx = NULL;
struct sc_card *card = NULL;
struct sc_pkcs15_card *p15card = NULL;

char * get_pin(struct sc_pkcs15_pin_info *pinfo)
{
	char buf[80];
	char *pincode;
	
	if (opt_pincode != NULL)
		return strdup(opt_pincode);
	sprintf(buf, "Enter PIN [%s]: ", pinfo->com_attr.label);
	while (1) {
		pincode = getpass(buf);
		if (strlen(pincode) == 0)
			return NULL;
		if (strlen(pincode) < pinfo->min_length ||
		    strlen(pincode) > pinfo->stored_length)
		    	continue;
		return pincode;
	}
}

int read_input(u8 *buf, int buflen)
{
	FILE *inf;
	int c;
	
	inf = fopen(opt_input, "r");
	if (inf == NULL) {
		fprintf(stderr, "Unable to open '%s' for reading.\n", opt_input);
		return -1;
	}
	c = fread(buf, 1, buflen, inf);
	fclose(inf);
	if (c < 0) {
		perror("read");
		return -1;
	}
	return c;
}

int write_output(const u8 *buf, int len)
{
	FILE *outf;
	int output_binary = 1;
	
	if (opt_output != NULL) {
		outf = fopen(opt_output, "w");
		if (outf == NULL) {
			fprintf(stderr, "Unable to open '%s' for writing.\n", opt_output);
			return -1;
		}
	} else {
		outf = stdout;
		output_binary = 0;
	}
	if (output_binary == 0)
		print_binary(outf, buf, len);
	else
		fwrite(buf, len, 1, outf);
	if (outf != stdout)
		fclose(outf);
	return 0;
}

int sign(struct sc_pkcs15_prkey_info *key)
{
	u8 buf[1024], out[1024];
	int r, c, len;
	
	if (opt_input == NULL) {
		fprintf(stderr, "No input file specified.\n");
		return 2;
	}
	if (opt_output == NULL) {
		fprintf(stderr, "No output file specified.\n");
		return 2;
	}
	c = read_input(buf, sizeof(buf));
	if (c < 0)
		return 2;
	len = sizeof(out);
	r = sc_pkcs15_compute_signature(p15card, key, opt_hash_type,
					buf, c, out, len);
	if (r < 0) {
		fprintf(stderr, "Compute signature failed: %s\n", sc_strerror(r));
		return 1;
	}
	r = write_output(out, r);
	
	return 0;
}

int decipher(struct sc_pkcs15_prkey_info *key)
{
	u8 buf[1024], out[1024];
	int r, c, len;
	
	if (opt_input == NULL) {
		fprintf(stderr, "No input file specified.\n");
		return 2;
	}
	c = read_input(buf, sizeof(buf));
	if (c < 0)
		return 2;
	len = sizeof(out);
	r = sc_pkcs15_decipher(p15card, key, buf, c, out, len);
	if (r < 0) {
		fprintf(stderr, "Decrypt failed: %s\n", sc_strerror(r));
		return 1;
	}
	r = write_output(out, r);
	
	return 0;
}

int main(int argc, char * const argv[])
{
	int err = 0, r, c, long_optind = 0;
	int do_decipher = 0;
	int do_sign = 0;
	int action_count = 0;
	struct sc_pkcs15_prkey_info *key;
	struct sc_pkcs15_pin_info *pin;
	struct sc_pkcs15_id id;
	char *pincode;
		
	while (1) {
		c = getopt_long(argc, argv, "sck:r:i:o:qp:d", options, &long_optind);
		if (c == -1)
			break;
		if (c == '?')
			print_usage_and_die("pkcs15-crypt");
		switch (c) {
		case 's':
			do_sign++;
			action_count++;
			break;
		case 'c':
			do_decipher++;
			action_count++;
			break;
		case 'k':
			opt_key_id = optarg;
			action_count++;
			break;
		case 'r':
			opt_reader = atoi(optarg);
			break;
		case 'i':
			opt_input = optarg;
			break;
		case 'o':
			opt_output = optarg;
			break;
		case OPT_SHA1:
			opt_hash_type = SC_PKCS15_HASH_SHA1;
			break;
		case 'q':
			quiet++;
			break;
		case 'd':
			opt_debug++;
			break;
		case 'p':
			opt_pincode = optarg;
			break;
		}
	}
	if (action_count == 0)
		print_usage_and_die("pkcs15-crypt");
	r = sc_establish_context(&ctx);
	if (r) {
		fprintf(stderr, "Failed to establish context: %s\n", sc_strerror(r));
		return 1;
	}
	ctx->use_std_output = 1;
	ctx->debug = opt_debug;
	if (opt_reader >= ctx->reader_count || opt_reader < 0) {
		fprintf(stderr, "Illegal reader number. Only %d reader(s) configured.\n", ctx->reader_count);
		err = 1;
		goto end;
	}
	if (sc_detect_card(ctx, opt_reader) != 1) {
		fprintf(stderr, "Card not present.\n");
		return 3;
	}
	if (!quiet)
		fprintf(stderr, "Connecting to card in reader %s...\n", ctx->readers[opt_reader]);
	r = sc_connect_card(ctx, opt_reader, &card);
	if (r) {
		fprintf(stderr, "Failed to connect to card: %s\n", sc_strerror(r));
		err = 1;
		goto end;
	}

#if 0
	r = sc_lock(card);
	if (r) {
		fprintf(stderr, "Unable to lock card: %s\n", sc_strerror(r));
		err = 1;
		goto end;
	}
#endif

	if (!quiet)
		fprintf(stderr, "Trying to find a PKCS#15 compatible card...\n");
	r = sc_pkcs15_bind(card, &p15card);
	if (r) {
		fprintf(stderr, "PKCS#15 initialization failed: %s\n", sc_strerror(r));
		err = 1;
		goto end;
	}
	if (!quiet)
		fprintf(stderr, "Found %s!\n", p15card->label);

	r = sc_pkcs15_enum_private_keys(p15card);
	if (r <= 0) {
		if (r == 0)
			r = SC_ERROR_OBJECT_NOT_FOUND;
		fprintf(stderr, "Private key enumeration failed: %s\n", sc_strerror(r));
		err = 1;
		goto end;
	}
	if (opt_key_id != NULL) {
		sc_pkcs15_hex_string_to_id(opt_key_id, &id);
		r = sc_pkcs15_find_prkey_by_id(p15card, &id, &key);
		if (r < 0) {
			fprintf(stderr, "Unable to find private key '%s': %s\n",
				opt_key_id, sc_strerror(r));
			err = 2;
			goto end;
		}
	} else
		key = &p15card->prkey_info[0];
	r = sc_pkcs15_find_pin_by_auth_id(p15card, &key->com_attr.auth_id, &pin);
	if (r) {
		fprintf(stderr, "Unable to find PIN code for private key: %s\n",
			sc_strerror(r));
		err = 1;
		goto end;
	}
	pincode = get_pin(pin);
	if (pincode == NULL) {
		err = 5;
		goto end;
	}
	r = sc_pkcs15_verify_pin(p15card, pin, (const u8 *) pincode, strlen(pincode));
	if (r) {
		fprintf(stderr, "PIN code verification failed: %s\n", sc_strerror(r));
		err = 5;
		goto end;
	}
	free(pincode);
	if (!quiet)
		fprintf(stderr, "PIN code correct.\n");
	if (do_decipher) {
		if ((err = decipher(key)))
			goto end;
		action_count--;
	}
	if (do_sign) {
		if ((err = sign(key)))
			goto end;
		action_count--;
	}
end:
	if (p15card)
		sc_pkcs15_unbind(p15card);
	if (card) {
#if 0
		sc_unlock(card);
#endif
		sc_disconnect_card(card);
	}
	if (ctx)
		sc_destroy_context(ctx);
	return err;
}
