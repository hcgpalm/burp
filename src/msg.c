#include "burp.h"
#include "prog.h"
#include "msg.h"
#include "counter.h"
#include "asyncio.h"
#include "handy.h"

int send_msg_fp(FILE *fp, char cmd, const char *buf, size_t s)
{
	//fprintf(fp, "%c%04X%s\n", cmd, (unsigned int)s, buf);
	fprintf(fp, "%c%04X", cmd, (unsigned int)s);
	fwrite(buf, 1, s, fp);
	fprintf(fp, "\n");
	return 0;
}

int send_msg_zp(gzFile zp, char cmd, const char *buf, size_t s)
{
	//gzprintf(zp, "%c%04X%s\n", cmd, s, buf);
	gzprintf(zp, "%c%04X", cmd, s);
	gzwrite(zp, buf, s);
	gzprintf(zp, "\n");
	return 0;
}

static int do_write(BFILE *bfd, FILE *fp, unsigned char *out, size_t outlen)
{
	int ret=0;
#ifdef HAVE_WIN32
	if((ret=bwrite(bfd, out, outlen))<=0)
	{
		logp("error when appending: %d\n", ret);
		async_write_str('e', "write failed");
		return -1;
	}
#else
	if((fp && (ret=fwrite(out, 1, outlen, fp))<=0))
	{
		logp("error when appending: %d\n", ret);
		async_write_str('e', "write failed");
		return -1;
	}
#endif
	return 0;
}

#define ZCHUNK 16000

static int do_inflate(z_stream *zstrm, BFILE *bfd, FILE *fp, unsigned char *out, unsigned char *buftouse, size_t lentouse)
{
	int zret=Z_OK;
	unsigned have=0;

	zstrm->avail_in=lentouse;
	zstrm->next_in=buftouse;

	do
	{
		zstrm->avail_out=ZCHUNK;
		zstrm->next_out=out;
		zret=inflate(zstrm, Z_NO_FLUSH);
		switch(zret)
		{
			case Z_NEED_DICT:
			  zret=Z_DATA_ERROR;
			case Z_DATA_ERROR:
			case Z_MEM_ERROR:
			  logp("zstrm inflate error: %d\n", zret);
			  return -1;
			  break;
		}
		have=ZCHUNK-zstrm->avail_out;
		if(!have) continue;

		if(do_write(bfd, fp, out, have))
		{
			return -1;
			break;
		}
	} while(!zstrm->avail_out);
	return 0;
}

int transfer_gzfile_in(BFILE *bfd, FILE *fp, char **bytes, const char *encpassword, struct cntr *cntr)
{
	char cmd;
	char *buf=NULL;
	size_t len=0;
	int quit=0;
	int ret=-1;
	unsigned char out[ZCHUNK];
	size_t doutlen=0;
	//unsigned char doutbuf[1000+EVP_MAX_BLOCK_LENGTH];
	unsigned char doutbuf[ZCHUNK-EVP_MAX_BLOCK_LENGTH];

	z_stream zstrm;

	EVP_CIPHER_CTX *enc_ctx=NULL;

//logp("in transfer_gzfile_in\n");

	zstrm.zalloc=Z_NULL;
	zstrm.zfree=Z_NULL;
	zstrm.opaque=Z_NULL;
	zstrm.avail_in=0;
	zstrm.next_in=Z_NULL;

	if(inflateInit2(&zstrm, (15+16)))
	{
		logp("unable to init inflate\n");
		return -1;
	}

	if(encpassword && !(enc_ctx=enc_setup(0, encpassword)))
	{
		inflateEnd(&zstrm);
		return -1;
	}

	while(!quit)
	{
		if(async_read(&cmd, &buf, &len))
		{
			if(enc_ctx)
			{
				EVP_CIPHER_CTX_cleanup(enc_ctx);
				free(enc_ctx);
			}
			inflateEnd(&zstrm);
			return -1;
		}

		//logp("transfer in: %c:%s\n", cmd, buf);
		switch(cmd)
		{
			case 'a': // append
				if(!fp && !bfd)
				{
					logp("given append, but no file to write to\n");
					async_write_str('e', "append with no file");
					quit++; ret=-1;
				}
				else
				{
					size_t lentouse;
					unsigned char *buftouse=NULL;
					// If doing decryption, it needs
					// to be done before uncompressing.
					if(enc_ctx)
					{
					  if(!EVP_CipherUpdate(enc_ctx,
						doutbuf, (int *)&doutlen,
						(unsigned char *)buf,
						len))
					  {
						logp("Decryption error\n");
						quit++; ret=-1;
					  	break;
					  }
					  if(!doutlen) break;
					  lentouse=doutlen;
					  buftouse=doutbuf;
					}
					else
					{
					  lentouse=len;
					  buftouse=(unsigned char *)buf;
					}
					//logp("want to write: %d\n", zstrm.avail_in);

					if(do_inflate(&zstrm, bfd, fp, out,
						buftouse, lentouse))
					{
						ret=-1; quit++;
						break;
					}
				}
				break;
			case 'x': // finish up
				if(enc_ctx)
				{
					if(!EVP_CipherFinal_ex(enc_ctx,
						doutbuf, (int *)&doutlen))
					{
						logp("Decryption failure at the end.\n");
						ret=-1; quit++;
						break;
					}
					if(doutlen && do_inflate(&zstrm, bfd,
					  fp, out, doutbuf, doutlen))
					{
						ret=-1; quit++;
						break;
					}
				}
				*bytes=buf;
				buf=NULL;
				quit++;
				ret=0;
				break;
			case 'w':
				logp("WARNING: %s\n", buf);
				do_filecounter(cntr, cmd, 0);
				break;
			default:
				logp("unknown append cmd: %c\n", cmd);
				quit++;
				ret=-1;
				break;
		}
		if(buf) free(buf);
		buf=NULL;
	}
	inflateEnd(&zstrm);
	if(enc_ctx)
	{
		EVP_CIPHER_CTX_cleanup(enc_ctx);
		free(enc_ctx);
	}
	if(ret) logp("transfer file returning: %d\n", ret);
	return ret;
}

FILE *open_file(const char *fname, const char *mode)
{
	FILE *fp=NULL;

	if(!(fp=fopen(fname, mode)))
	{
		logp("could not open %s: %s\n", fname, strerror(errno));
		return NULL; 
	}
	return fp;
}

gzFile gzopen_file(const char *fname, const char *mode)
{
	gzFile fp=NULL;

	if(!(fp=gzopen(fname, mode)))
	{
		logp("could not open %s: %s\n", fname, strerror(errno));
		return NULL; 
	}
	return fp;
}