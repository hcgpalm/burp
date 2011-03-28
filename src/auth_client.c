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
#include "auth_client.h"

int authorise_client(struct config *conf)
{
	if(async_write_str('c', "hello")
	  || async_read_expect('c', "whoareyou")
	  || async_write_str('c', conf->cname)
	  || async_read_expect('c', "okpassword")
	  || async_write_str('c', conf->password)
	  || async_read_expect('c', "ok"))
	{
		logp("problem with auth\n");
		return -1;
	}
	logp("auth ok\n");
	return 0;
}