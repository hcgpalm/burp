#include "burp.h"
#include "prog.h"
#include "msg.h"
#include "lock.h"
#include "rs_buf.h"
#include "handy.h"
#include "asyncio.h"
#include "zlibio.h"
#include "counter.h"
#include "dpth.h"
#include "sbuf.h"
#include "berrno.h"

static int load_signature_and_send_delta(const char *rpath, unsigned long long *bytes, unsigned long long *sentbytes, struct cntr *cntr)
{
	rs_job_t *job;
	rs_result r;
	rs_signature_t *sumset=NULL;
	unsigned char checksum[MD5_DIGEST_LENGTH+1];
#ifdef HAVE_WIN32
        BFILE bfd;
#else
	FILE *in=NULL;
#endif
	rs_filebuf_t *infb=NULL;
	rs_filebuf_t *outfb=NULL;
	rs_buffers_t rsbuf;
	memset(&rsbuf, 0, sizeof(rsbuf));

//logp("loadsig %s\n", rpath);

	job = rs_loadsig_begin(&sumset);
	if((r=do_rs_run(job, NULL, NULL, NULL, NULL, NULL, async_get_fd(), -1, cntr)))
	{
		rs_free_sumset(sumset);
		return r;
	}
	if((r=rs_build_hash_table(sumset)))
	{
		rs_free_sumset(sumset);
		return r;
	}
	rs_job_free(job);
	job=NULL;
//logp("end loadsig\n");
//logp("\n");

#ifdef HAVE_WIN32
        binit(&bfd);
        if(bopen(&bfd, rpath, O_RDONLY | O_BINARY | O_NOATIME, S_IRUSR | S_IWUSR)<0)
        {
                berrno be;
                logp("Could not open %s: %s\n", rpath, be.bstrerror(errno));
                return -1;
        }
#else
	//logp("opening: %s\n", rpath);
	if(!(in=fopen(rpath, "rb")))
	{
		logp("could not open '%s' in order to generate delta.\n",
			rpath);
		rs_free_sumset(sumset);
		return RS_IO_ERROR;
	}
#endif
//logp("start delta\n");

	if(!(job=rs_delta_begin(sumset)))
	{
		logp("could not start delta job.\n");
		rs_free_sumset(sumset);
		return RS_IO_ERROR;
	}

#ifdef HAVE_WIN32
	if(!(infb=rs_filebuf_new(&bfd, NULL, NULL, -1, rs_inbuflen, cntr))
	  || !(outfb=rs_filebuf_new(NULL, NULL, NULL, async_get_fd(), rs_outbuflen, cntr)))
	{
		logp("could not rs_filebuf_new for delta\n");
		if(infb) rs_filebuf_free(infb);
		return -1;
	}
#else
	if(!(infb=rs_filebuf_new(NULL, in, NULL, -1, rs_inbuflen, cntr))
	  || !(outfb=rs_filebuf_new(NULL, NULL, NULL, async_get_fd(), rs_outbuflen, cntr)))
	{
		logp("could not rs_filebuf_new for delta\n");
		if(infb) rs_filebuf_free(infb);
		return -1;
	}
#endif
//logp("start delta loop\n");

	while(1)
	{
		size_t wlen=0;
		rs_result delresult;
		delresult=rs_async(job, &rsbuf, infb, outfb);
		if(delresult==RS_DONE)
		{
			r=delresult;
//			logp("delresult done\n");
			break;
		}
		else if(delresult==RS_BLOCKED || delresult==RS_RUNNING)
		{
//			logp("delresult running/blocked: %d\n", delresult);
			// Keep going
		}
		else
		{
			logp("error in rs_async for delta: %d\n", delresult);
			r=delresult;
			break;
		}
		// FIX ME: get it to read stuff (errors, for example) here too.
		if(async_rw(NULL, NULL, '\0', '\0', NULL, &wlen))
			return -1;
	}

	if(r!=RS_DONE)
		logp("delta loop returned: %d\n", r);

//logp("after delta loop: %d\n", r);
//logp("\n");

	if(r==RS_DONE)
	{
		*bytes=infb->bytes;
		*sentbytes=outfb->bytes;
		if(!MD5_Final(checksum, &(infb->md5)))
		{
			logp("MD5_Final() failed\n");
			r=RS_IO_ERROR;
		}
	}
	rs_filebuf_free(infb);
	rs_filebuf_free(outfb);
	rs_job_free(job);
	rs_free_sumset(sumset);
#ifdef HAVE_WIN32
        bclose(&bfd);
#else
	if(in) fclose(in);
#endif

	if(r==RS_DONE && write_endfile(*bytes, checksum)) // finish delta file
			return -1;

	//logp("end of load_sig_send_delta for: %s\n", rpath);

	return r;
}

static int do_backup_phase2_client(struct config *conf, struct cntr *cntr)
{
	int ret=0;
	int quit=0;
	char cmd;
	char *buf=NULL;
	size_t len=0;
	char attribs[MAXSTRING];

	struct sbuf sb;

	init_sbuf(&sb);

	if(async_write_str('c', "backupphase2")
	  || async_read_expect('c', "ok"))
		return -1;

	while(!quit)
	{
		if(async_read(&cmd, &buf, &len))
		{
			ret=-1;
			quit++;
		}
		else if(buf)
		{
			//logp("got: %c:%s\n", cmd, buf);
			if(cmd=='t')
			{
				sb.datapth=buf;
				buf=NULL;
				continue;
			}
			else if(cmd=='r')
			{
				// Ignore the stat data - we will fill it
				// in again. Some time may have passed by now,
				// and it is best to make it as fresh as
				// possible.
				free(buf);
				buf=NULL;
				continue;
			}
			else if(cmd=='f' || cmd=='y')
					// normal file || encrypted file
			{
				struct stat statbuf;
				unsigned long long bytes=0;

				sb.path=buf;
				buf=NULL;

				if(lstat(sb.path, &statbuf))
				{
					logw(cntr, "File has vanished: %s", sb.path);
					free_sbuf(&sb);
					continue;
				}
				encode_stat(attribs, &statbuf);

				if(cmd=='f' && sb.datapth)
				{
					unsigned long long sentbytes=0;
					// Need to do sig/delta stuff.
					if(async_write_str('t', sb.datapth)
					  || async_write_str('r', attribs)
					  || async_write_str('f', sb.path)
					  || load_signature_and_send_delta(
						sb.path, &bytes, &sentbytes, cntr))
					{
						logp("error in sig/delta for %s (%s)\n", sb.path, sb.datapth);
						ret=-1;
						quit++;
					}
					else
					{
						do_filecounter(cntr, 'x', 1);
						do_filecounter_bytes(cntr, bytes);
						do_filecounter_sentbytes(cntr, sentbytes);
					}
				}
				else
				{
					//logp("need to send whole file: %s\n",
					//	sb.path);
					// send the whole file.
					if(async_write_str('r', attribs)
					  || async_write_str(cmd, sb.path)
					  || send_whole_file_gz(sb.path,
						NULL, 0, &bytes,
						conf->encryption_password, cntr))
					{
						ret=-1;
						quit++;
					}
					else
					{
						do_filecounter(cntr, 'F', 1);
						do_filecounter_bytes(cntr, bytes);
						do_filecounter_sentbytes(cntr, bytes);
					}
				}
				free_sbuf(&sb);
			}
			else if(cmd=='w')
			{
				do_filecounter(cntr, cmd, 0);
				free(buf);
				buf=NULL;
			}
			else if(cmd=='c' && !strcmp(buf, "backupphase2end"))
			{
				if(async_write_str('c', "okbackupphase2end"))
					ret=-1;
				quit++;
			}
			else
			{
				logp("unexpected cmd from server: %c %s\n",
					cmd, buf);
				ret=-1;
				quit++;
				free(buf);
				buf=NULL;
			}
		}
	}
	return ret;
}

int backup_phase2_client(struct config *conf, struct cntr *cntr)
{
	int ret=0;

	logp("Phase 2 begin (send file data)\n");
        reset_filecounter(cntr);

	ret=do_backup_phase2_client(conf, cntr);

        end_filecounter(cntr, 1, ACTION_BACKUP);

	if(ret) logp("Error in phase 2\n");
	logp("Phase 2 end (send file data)\n");

	return ret;
}