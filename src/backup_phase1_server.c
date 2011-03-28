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
#include "backup_phase1_server.h"

int backup_phase1_server(const char *phase1data, const char *client, struct cntr *cntr)
{
	char cmd;
	int ret=0;
	int quit=0;
	size_t len=0;
	char *buf=NULL;
	gzFile p1zp=NULL;
	char *phase1tmp=NULL;
	int expect_file_type=0;
	char *lastfile=NULL;

	logp("Begin phase1 (file system scan)\n");

	if(!(phase1tmp=get_tmp_filename(phase1data)))
		return -1;

	if(!(p1zp=gzopen_file(phase1tmp, "wb9")))
	{
		free(phase1tmp);
		return -1;
	}

	while(!quit)
	{
		write_status(client, 1, lastfile, cntr);
		if(async_read(&cmd, &buf, &len))
		{
			quit++; ret=-1;
			break;
		}
		if(cmd=='c')
		{
			if(!strcmp(buf, "backupphase2"))
			{
				if(async_write_str('c', "ok")) ret=-1;
				send_msg_zp(p1zp, 'c',
					"phase1end", strlen("phase1end"));
				break;
			}
			else
			{
				quit++; ret=-1;
				logp("unexpected cmd in backupphase1: %c %s\n",
					cmd, buf);
			}
		}
		else if(cmd=='w')
		{
			logp("WARNING: %s\n", buf);
			do_filecounter(cntr, cmd, 0);
		}
		else
		{
			send_msg_zp(p1zp, cmd, buf, len);
			// TODO - Flaky, do this better
			if(cmd=='r') expect_file_type++;
			else if(expect_file_type)
			{
				expect_file_type=0;
				do_filecounter(cntr, cmd, 0);
				if(lastfile) free(lastfile);
				lastfile=buf; buf=NULL;
				continue;
			}
		}
		if(buf) { free(buf); buf=NULL; }
	}
	if(buf) { free(buf); buf=NULL; }
	if(lastfile) { free(lastfile); lastfile=NULL; }

        if(p1zp) gzclose(p1zp);
	if(!ret && do_rename(phase1tmp, phase1data))
		ret=-1;
	free(phase1tmp);

	end_filecounter(cntr, 0, ACTION_BACKUP);

	logp("End phase1 (file system scan)\n");

	return ret;
}