#include "burp.h"
#include "prog.h"
#include "counter.h"
#include "asyncio.h"
#include "librsync.h"
#include "handy.h"
#include "find.h"
#include "ssl.h"

static int fd=-1;
static SSL *ssl=NULL;

static char *readbuf=NULL;
static size_t readbuflen=0;
static size_t readbufmaxsize=(rs_inbuflen*2)+32;

static char *writebuf=NULL;
static size_t writebuflen=0;
static size_t writebufmaxsize=(rs_outbuflen*2)+32;

int status_wfd=-1; // for the client to send information to the server.

static void truncate_buf(char **buf, size_t *buflen)
{
	(*buf)[0]='\0';
	*buflen=0;
}

static int parse_readbuf(char *cmd, char **dest, size_t *rlen)
{
	unsigned int s=0;
	char cmdtmp='\0';

	if(readbuflen>=5)
	{
		if((sscanf(readbuf, "%c%04X", &cmdtmp, &s))!=2)
		{
			logp("sscanf of '%s' failed in parse_readbuf\n",
				readbuf);
			truncate_buf(&readbuf, &readbuflen);
			return -1;
		}
	}
	if(readbuflen>=s+5)
	{
		*cmd=cmdtmp;
		if(!(*dest=(char *)malloc(s+1)))
		{
			logp("out of memory in parse_readbuf\n");
			truncate_buf(&readbuf, &readbuflen);
			return -1;
		}
		if(!(memcpy(*dest, readbuf+5, s)))
		{
			logp("memcpy failed in parse_readbuf\n");
			truncate_buf(&readbuf, &readbuflen);
			return -1;
		}
		(*dest)[s]='\0';
		if(!(memmove(readbuf, readbuf+s+5, readbuflen-s-5)))
		{
			logp("memmove failed in parse_readbuf\n");
			truncate_buf(&readbuf, &readbuflen);
			return -1;
		}
		readbuflen-=s+5;
		*rlen=s;
	}
	return 0;
}

static int async_alloc_buf(char **buf, size_t *buflen, size_t bufmaxsize)
{
	if(!*buf)
	{
		if(!(*buf=(char *)malloc(bufmaxsize)))
		{
			logp("out of memory in async_alloc_buf\n");
			return -1;
		}
		truncate_buf(buf, buflen);
	}
	return 0;
}

static int do_read(int *read_blocked_on_write)
{
	ssize_t r;

	r=SSL_read(ssl, readbuf+readbuflen, readbufmaxsize-readbuflen);

	switch(SSL_get_error(ssl, r))
	{
	  case SSL_ERROR_NONE:
		readbuflen+=r;
		readbuf[readbuflen]='\0';
		break;
	  case SSL_ERROR_ZERO_RETURN:
		/* end of data */
		//logp("zero return!\n");
		SSL_shutdown(ssl);
		truncate_buf(&readbuf, &readbuflen);
		return -1;
	  case SSL_ERROR_WANT_READ:
		break;
	  case SSL_ERROR_WANT_WRITE:
		*read_blocked_on_write=1;
		break;
	  default:
		logp("SSL read problem\n");
		truncate_buf(&readbuf, &readbuflen);
		return -1;
	}
	return 0;
}

static int do_write(int *write_blocked_on_read)
{
	ssize_t w;

	w=SSL_write(ssl, writebuf, writebuflen);

	switch(SSL_get_error(ssl, w))
	{
	  case SSL_ERROR_NONE:
		//logp("wrote: %d\n", w);
		memmove(writebuf, writebuf+w, writebuflen-w);
		writebuflen-=w;
		break;
	  case SSL_ERROR_WANT_WRITE:
		break;
	  case SSL_ERROR_WANT_READ:
		*write_blocked_on_read=1;
		break;
	  default:
		berr_exit("SSL write problem");
		logp("write returned: %d\n", w);
		return -1;
	}
	return 0;
}

static int append_to_write_buffer(const char *buf, size_t len)
{
	memcpy(writebuf+writebuflen, buf, len);
	writebuflen+=len;
	writebuf[writebuflen]='\0';
	return 0;
}

int async_append_all_to_write_buffer(char wcmd, const char *wsrc, size_t *wlen)
{
	size_t sblen=0;
	char sbuf[10]="";
	if(writebuflen+6+(*wlen) >= writebufmaxsize-1)
		return 1;

	snprintf(sbuf, sizeof(sbuf), "%c%04X", wcmd, (unsigned int)*wlen);
	sblen=strlen(sbuf);
	append_to_write_buffer(sbuf, sblen);
	append_to_write_buffer(wsrc, *wlen);
	//logp("appended to wbuf: %c (%d) (%d)\n", wcmd, *wlen+sblen, writebuflen);
	*wlen=0;
	return 0;
}

int async_init(int afd, SSL *assl)
{
	fd=afd;
	ssl=assl;
	if(async_alloc_buf(&readbuf, &readbuflen, readbufmaxsize)
	  || async_alloc_buf(&writebuf, &writebuflen, writebufmaxsize))
		return -1;
	return 0;

}

void async_free(void)
{
	//printf("in async_free\n");
	if(ssl && fd>=0)
	{
		int r;
		//int x;
		set_blocking(fd);
/* I do not think this SSL_shutdown stuff works right. Ignore it for now. */
//printf("calling SSL_shutdown...\n");
		if(!(r=SSL_shutdown(ssl)))
		{
//printf("calling SSL_shutdown again...\n");
			shutdown(fd, 1);
			r=SSL_shutdown(ssl);
		}
/*
		switch(r)
		{
			case 1:
				printf("SSL shutdown OK\n");
				break; // success
			case 0:
			case -1:
			default:
				switch(x=SSL_get_error(ssl, r))
				{
					case SSL_ERROR_NONE:
						printf("A!\n"); break;
					case SSL_ERROR_ZERO_RETURN:
						printf("B!\n"); break;
					case SSL_ERROR_WANT_READ:
						printf("C!\n");
						do_read(&r);
						break;
					case SSL_ERROR_WANT_WRITE:
						printf("D!\n"); break;
					default:
						printf("Z: %d!\n", x); break;
				}
				printf("SSL shutdown failed: %d\n", r);
		}
*/
	}
	if(ssl)
	{
		SSL_free(ssl);
		ssl=NULL;
	}
	close_fd(&fd);
	readbuflen=0;
	writebuflen=0;
	if(readbuf) { free(readbuf); readbuf=NULL; }
	if(writebuf) { free(writebuf); writebuf=NULL; }
}

/* for debug purposes */
static int setsec=1;
static int setusec=1;

void settimers(int sec, int usec)
{
	setsec=sec;
	setusec=usec;
}

int async_rw(char *rcmd, char **rdst, size_t *rlen, char wcmd, const char *wsrc, size_t *wlen)
{
        int mfd=-1;
        fd_set fsr;
        fd_set fsw;
        fd_set fse;
	int doread=0;
	int dowrite=0;
        struct timeval tval;
	static int read_blocked_on_write=0;
	static int write_blocked_on_read=0;

//printf("in async_rw\n");

	if(fd<0)
	{
		logp("fd not ready in async rw: %d\n", fd);
		return -1;
	}

	if(rdst) doread++; // Given a pointer to allocate and read into.

	if(*wlen)
	{
		// More stuff to append to the write buffer.
		async_append_all_to_write_buffer(wcmd, wsrc, wlen);
	}

	if(writebuflen && !write_blocked_on_read)
		dowrite++; // The write buffer is not yet empty.

	if(doread)
	{
		if(parse_readbuf(rcmd, rdst, rlen))
		{
			logp("error in parse_readbuf\n");
			return -1;
		}
		if(*rcmd && *rdst) return 0;

		if(read_blocked_on_write) doread=0;
	}

        if(doread || dowrite)
        {
//printf("async_rw loop read %d write %d wbuflen: %d\n", doread, dowrite, writebuflen);
                mfd=-1;

                if(doread) FD_ZERO(&fsr);
                if(dowrite) FD_ZERO(&fsw);
                FD_ZERO(&fse);

                add_fd_to_sets(fd,
			doread?&fsr:NULL, dowrite?&fsw:NULL, &fse, &mfd);

                tval.tv_sec=setsec;
                tval.tv_usec=setusec;

                if(select(mfd+1,
			doread?&fsr:NULL, dowrite?&fsw:NULL, &fse, &tval)<0)
                {
                        if(errno!=EAGAIN && errno!=EINTR)
                        {
                                logp("select error: %s\n", strerror(errno));
                                return -1;
                        }
                }

		if(!FD_ISSET(fd, &fse)
		  && (!doread || !FD_ISSET(fd, &fsr))
		  && (!dowrite || !FD_ISSET(fd, &fsw)))
		{
	//		logp("SELECT HIT TIMEOUT - doread: %d, dowrite: %d\n",
	//			doread, dowrite);
			return 0;
		}

                if(FD_ISSET(fd, &fse))
                {
                        logp("error on socket\n");
                        return -1;
                }

                if(doread && FD_ISSET(fd, &fsr)) // able to read
                {
			int r;
			read_blocked_on_write=0;
			if(do_read(&read_blocked_on_write)) return -1;
			if((r=parse_readbuf(rcmd, rdst, rlen)))
				logp("error in second parse_readbuf\n");
			return r;
                }

                if(dowrite && FD_ISSET(fd, &fsw)) // able to write
		{
			int r;
			write_blocked_on_read=0;
			if((r=do_write(&write_blocked_on_read)))
				logp("error in do_write\n");
			return r;
		}
        }

        return 0;
}

int async_rw_ensure_read(char *rcmd, char **rdst, size_t *rlen, char wcmd, const char *wsrc, size_t wlen)
{
	size_t w=wlen;
	while(!*rdst) if(async_rw(rcmd, rdst, rlen, wcmd, wsrc, &w))
		return -1;
	return 0;
}

int async_rw_ensure_write(char *rcmd, char **rdst, size_t *rlen, char wcmd, const char *wsrc, size_t wlen)
{
	size_t w=wlen;
	while(w) if(async_rw(rcmd, rdst, rlen, wcmd, wsrc, &w))
		return -1;
	return 0;
}

int async_read_quick(char *rcmd, char **rdst, size_t *rlen)
{
	int r;
	size_t w=0;
	int savesec=setsec;
	int saveusec=setusec;
	setsec=0;
	setusec=0;
	r=async_rw(rcmd, rdst, rlen, '\0', NULL, &w);
	setsec=savesec;
	setusec=saveusec;
	return r;
}

int async_read(char *rcmd, char **rdst, size_t *rlen)
{
	return async_rw_ensure_read(rcmd, rdst, rlen, '\0', NULL, 0);
}

int async_write(char wcmd, const char *wsrc, size_t wlen)
{
	return async_rw_ensure_write(NULL, NULL, NULL, wcmd, wsrc, wlen);
}

int async_write_str(char wcmd, const char *wsrc)
{
	size_t w;
	w=strlen(wsrc);
	return async_write(wcmd, wsrc, w);
}

int async_read_expect(char cmd, const char *expect)
{
	int ret=0;
	char rcmd;
	char *rdst=NULL;
	size_t rlen=0;
	if(async_read(&rcmd, &rdst, &rlen)) return -1;
	if(rcmd!=cmd || strcmp(rdst, expect))
	{
		logp("expected '%c:%s', got '%c:%s'\n",
			cmd, expect, rcmd, rdst);
		ret=-1;
	}
	free(rdst);
	return ret;
}


static int async_read_fp_msg(FILE *fp, gzFile zp, char **buf, size_t len)
{
	char *b=NULL;
	ssize_t r=0;

	/* Now we know how long the data is, so read it. */
	if(!(*buf=(char *)malloc(len+1)))
	{
		logp("could not malloc %d\n", len+1);
		return -1;
	}

	b=*buf;
	while(len>0)
	{
		if((zp && (r=gzread(zp, b, len))<=0)
		  || (fp && (r=fread(b, 1, len, fp))<=0))
		{
			//logp("read returned: %d\n", r);
			if(*buf) free(*buf);
			*buf=NULL;
			if(r==0)
			{
				if(zp && gzeof(zp)) return 1;
				if(fp && feof(fp)) return 1;
			}
			return -1;
		}
		b+=r;
		len-=r;
	}
	*b='\0';
	//logp("read_msg: %s\n", *buf);
	return 0;
}

int async_read_fp(FILE *fp, gzFile zp, char *cmd, char **rdst, size_t *rlen)
{
	int asr;
	unsigned int r;
	char *tmp=NULL;

	// First, get the command and length
	if((asr=async_read_fp_msg(fp, zp, &tmp, 5)))
	{
		if(tmp) free(tmp);
		return asr;
	}

	if((sscanf(tmp, "%c%04X", cmd, &r))!=2)
	{
		logp("sscanf of '%s' failed\n", tmp);
		if(tmp) free(tmp);
		return -1;
	}
	*rlen=r;
	if(tmp) free(tmp);

	if(!(asr=async_read_fp_msg(fp, zp, rdst, (*rlen)+1))) // +1 for '\n'
		(*rdst)[*rlen]='\0'; // remove new line.

	return asr;
}

void log_and_send(const char *msg)
{
	logp("%s\n", msg);
	if(fd>0) async_write_str('e', msg);
}

int async_get_fd(void)
{
	return fd;
}



/* Read from fp if given - else read from our fd */
int async_read_stat(FILE *fp, gzFile zp, char **buf, size_t *len, struct stat *statp, char **dpth, struct cntr *cntr)
{
	char cmd;
	char *d=NULL;
	while(1)
	{
		if(fp || zp)
		{
			int asr;
			if((asr=async_read_fp(fp, zp, &cmd, buf, len)))
			{
				//logp("async_read_fp returned: %d\n", asr);
				if(d) free(d);
				return asr;
			}
			if((*buf)[*len]=='\n') (*buf)[*len]='\0';
		}
		else
		{
			if(async_read(&cmd, buf, len))
			{
				break;
			}
			if(cmd=='w')
			{
				logp("WARNING: %s\n", buf);
				do_filecounter(cntr, cmd, 0);
				if(*buf) { free(*buf); *buf=NULL; }
				continue;
			}
		}
		if(cmd=='t')
		{
			if(d) free(d);
			d=*buf;
			*buf=NULL;
		}
		else if(cmd=='r')
		{
			if(statp) decode_stat(*buf, statp);
			if(*dpth) free(*dpth);
			*dpth=d;
			return 0;
		}
		else if((cmd=='c' && !strcmp(*buf, "backupend"))
		  || (cmd=='c' && !strcmp(*buf, "restoreend"))
		  || (cmd=='c' && !strcmp(*buf, "phase1end")))
		{
			if(d) free(d);
			return 1;
		}
		else if(cmd=='L' || cmd=='l')
		{
			// LOOK OUT! This is a kludge to make the restore
			// interrupt work when a link already exists.
			// TODO: The restore stuff should use the sbuf stuff
			// instead  of trying to parse the manifest stream
			// itself.
			if(*buf) { free(*buf); *buf=NULL; }
		}
		else
		{
			logp("expected cmd 't' or 'r', got '%c:%s'\n", cmd, *buf?*buf:"");
			if(*buf) { free(*buf); *buf=NULL; }
			break;
		}
	}
	if(d) free(d);
	return -1;
}

// should be in src/lib/log.c
int logw(struct cntr *cntr, const char *fmt, ...)
{
	int r=0;
	char buf[512]="";
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	r=async_write_str('w', buf);
	va_end(ap);
	do_filecounter(cntr, 'w', 1);
	return r;
}