#ifndef _CONF_FILE_H
#define _CONF_FILE_H

enum burp_mode
{
	MODE_UNSET=0,
	MODE_SERVER,
	MODE_CLIENT
};

typedef struct backupdir backupdir_t;

struct backupdir
{
	int include;
	char *path;
};

struct config
{
	int port;
	int status_port;
	enum burp_mode mode;
	char *lockfile;
	char *ssl_cert_ca;
	char *ssl_cert;
	char *ssl_cert_password;
	char *ssl_peer_cn;

// server options
	char *directory;
	char *clientconfdir;
	char *ssl_dhfile;
	int max_children;

// client options
	char *cname;
	char *password;
	char *server;
	struct backupdir **startdir;
	struct backupdir **incexcdir;
	struct backupdir **fschgdir;
	int sdcount;
	int iecount;
	int fscount;
	int cross_all_filesystems;
	char *encryption_password;

// Client options on the server.
// They can be set globally in the server config, or for each client.
	int keep;
	int hardlinked_archive;
	char *working_dir_recovery_method;

	char *timer_script;
	struct backupdir **timer_arg;
	int tacount;

	char *notify_success_script;
	struct backupdir **notify_success_arg;
	int nscount;

	char *notify_failure_script;
	struct backupdir **notify_failure_arg;
	int nfcount;
};

extern void init_config(struct config *conf);
extern int load_config(const char *config_path, struct config *conf, bool loadall);
extern void free_config(struct config *conf);
extern int set_client_global_config(struct config *conf, struct config *cconf);

#endif