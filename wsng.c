#include	<stdio.h>
#include	<stdlib.h>
#include	<strings.h>
#include	<string.h>
#include	<netdb.h>
#include	<errno.h>
#include	<unistd.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<sys/param.h>
#include	<signal.h>
#include	"socklib.h"
#include    <time.h>

struct file_info {
    char* extension;
    char* content;
};

struct file_info content_types[] = {
    { "html", "text/html" },
    { "jpg", "image/jpeg" },
    { "jpeg", "image/jpeg" },
    { "gif", "image/gif" },
    { NULL, NULL }
};

/*
 * ws.c - a web server
 *
 *    usage: ws [ -c configfilenmame ]
 * features: supports the GET command only
 *           runs in the current directory
 *           forks a new child to handle each request
 *           needs many additional features 
 *
 *  compile: cc ws.c socklib.c -o ws
 *  history: 2018-04-21 added SIGINT handling (mk had it)
 *  history: 2012-04-23 removed extern declaration for fdopen (it's in stdio.h)
 *  history: 2012-04-21 more minor cleanups, expanded some fcn comments
 *  history: 2010-04-24 cleaned code, merged some of MK's ideas
 *  history: 2008-05-01 removed extra fclose that was causing double free
 */


#define	PORTNUM	80
#define	SERVER_ROOT	"."
#define	CONFIG_FILE	"wsng.conf"
#define	VERSION		"1"

#define	MAX_RQ_LEN	4096
#define	LINELEN		1024
#define	PARAM_LEN	128
#define	VALUE_LEN	512

char	myhost[MAXHOSTNAMELEN];
int	myport;
char	*full_hostname();

#define	oops(m,x)	{ perror(m); exit(x); }

/*
 * prototypes
 */

int	startup(int, char *a[], char [], int *);
void	read_til_crnl(FILE *);
void	process_rq( char *, FILE *);
void	bad_request(FILE *);
void	cannot_do(FILE *fp);
void	do_404(char *item, FILE *fp);
void	do_cat(char *f, FILE *fpsock);
void	do_exec( char *prog, FILE *fp);
void	do_ls(char *dir, FILE *fp);
int	ends_in_cgi(char *f);
char 	*file_type(char *f);
void	header( FILE *fp, int code, char *msg, char *content_type );
int	isadir(char *f);
char	*modify_argument(char *arg, int len);
int	not_exist(char *f);
void	fatal(char *, char *);
void	handle_call(int);
int	read_request(FILE *, char *, int);
char	*readline(char *, int, FILE *);

int	mysocket = -1;		/* for SIGINT handler */

int
main(int ac, char *av[])
{
	int 	sock, fd;

	/* set up */
	sock = startup(ac, av, myhost, &myport);
	mysocket = sock;

	/* sign on */
	printf("wsng%s started.  host=%s port=%d\n", VERSION, myhost, myport);

	/* main loop here */
	while(1)
	{
		fd    = accept( sock, NULL, NULL );	/* take a call	*/
		if ( fd == -1 )
			perror("accept");
		else
			handle_call(fd);		/* handle call	*/
	}
	return 0;
	/* never end */
}

/*
 * handle_call(fd) - serve the request arriving on fd
 * summary: fork, then get request, then process request
 *    rets: child exits with 1 for error, 0 for ok
 *    note: closes fd in parent
 */
void handle_call(int fd)
{
	int	pid = fork();
	FILE	*fpin, *fpout;
	char	request[MAX_RQ_LEN];

	if ( pid == -1 ){
		perror("fork");
		return;
	}

	/* child: buffer socket and talk with client */
	if ( pid == 0 )
	{
		fpin  = fdopen(fd, "r");
		fpout = fdopen(fd, "w");
		if ( fpin == NULL || fpout == NULL )
			exit(1);

		if ( read_request(fpin, request, MAX_RQ_LEN) == -1 )
			exit(1);
		printf("got a call: request = %s", request);

		process_rq(request, fpout);
		fflush(fpout);		/* send data to client	*/
		exit(0);		/* child is done	*/
					/* exit closes files	*/
	}
	/* parent: close fd and return to take next call	*/
	close(fd);
}

/*
 * read the http request into rq not to exceed rqlen
 * return -1 for error, 0 for success
 */
int read_request(FILE *fp, char rq[], int rqlen)
{
	/* null means EOF or error. Either way there is no request */
	if ( readline(rq, rqlen, fp) == NULL )
		return -1;
	read_til_crnl(fp);
	return 0;
}

void read_til_crnl(FILE *fp)
{
        char    buf[MAX_RQ_LEN];
        while( readline(buf,MAX_RQ_LEN,fp) != NULL 
			&& strcmp(buf,"\r\n") != 0 )
                ;
}

/*
 * readline -- read in a line from fp, stop at \n 
 *    args: buf - place to store line
 *          len - size of buffer
 *          fp  - input stream
 *    rets: NULL at EOF else the buffer
 *    note: will not overflow buffer, but will read until \n or EOF
 *          thus will lose data if line exceeds len-2 chars
 *    note: like fgets but will always read until \n even if it loses data
 */
char *readline(char *buf, int len, FILE *fp)
{
        int     space = len - 2;
        char    *cp = buf;
        int     c;

        while( ( c = getc(fp) ) != '\n' && c != EOF ){
                if ( space-- > 0 )
                        *cp++ = c;
        }
        if ( c == '\n' )
                *cp++ = c;
        *cp = '\0';
        return ( c == EOF && cp == buf ? NULL : buf );
}
/*
 * initialization function
 * 	1. process command line args
 *		handles -c configfile
 *	2. open config file
 *		read rootdir, port
 *	3. chdir to rootdir
 *	4. open a socket on port
 *	5. gets the hostname
 *	6. return the socket
 *       later, it might set up logfiles, check config files,
 *         arrange to handle signals
 *
 *  returns: socket as the return value
 *	     the host by writing it into host[]
 *	     the port by writing it into *portnump
 */
int startup(int ac, char *av[], char host[], int *portnump)
{
	int	sock;
	int	portnum     = PORTNUM;
	char	*configfile = CONFIG_FILE ;
	int	pos;
	void	process_config_file(char *, int *);
	void	done(int);

	signal(SIGINT, done);
	for(pos=1;pos<ac;pos++){
		if ( strcmp(av[pos],"-c") == 0 ){
			if ( ++pos < ac )
				configfile = av[pos];
			else
				fatal("missing arg for -c",NULL);
		}
	}
	process_config_file(configfile, &portnum);
			
	sock = make_server_socket( portnum );
	if ( sock == -1 ) 
		oops("making socket",2);
	strcpy(myhost, full_hostname());
	*portnump = portnum;
	return sock;
}


/*
 * opens file or dies
 * reads file for lines with the format
 *   port ###
 *   server_root path
 * at the end, return the portnum by loading *portnump
 * and chdir to the rootdir
 */
void process_config_file(char *conf_file, int *portnump)
{
	FILE	*fp;
	char	rootdir[VALUE_LEN] = SERVER_ROOT;
	char	param[PARAM_LEN];
	char	value[VALUE_LEN];
	int	port;
	int	read_param(FILE *, char *, int, char *, int );

	/* open the file */
	if ( (fp = fopen(conf_file,"r")) == NULL )
		fatal("Cannot open config file %s", conf_file);

	/* extract the settings */
	while( read_param(fp, param, PARAM_LEN, value, VALUE_LEN) != EOF )
	{
		if ( strcasecmp(param,"server_root") == 0 )
			strcpy(rootdir, value);
		if ( strcasecmp(param,"port") == 0 )
			port = atoi(value);
	}
	fclose(fp);

	/* act on the settings */
	if (chdir(rootdir) == -1)
		oops("cannot change to rootdir", 2);
	*portnump = port;
	return;
}

/*
 * read_param:
 *   purpose -- read next parameter setting line from fp
 *   details -- a param-setting line looks like  name value
 *		for example:  port 4444
 *     extra -- skip over lines that start with # and those
 *		that do not contain two strings
 *   returns -- EOF at eof and 1 on good data
 *
 */
int read_param(FILE *fp, char *name, int nlen, char* value, int vlen)
{
	char	line[LINELEN];
	int	c;
	char	fmt[100] ;

	sprintf(fmt, "%%%ds%%%ds", nlen, vlen);

	/* read in next line and if the line is too long, read until \n */
	while( fgets(line, LINELEN, fp) != NULL )
	{
		if ( line[strlen(line)-1] != '\n' )
			while( (c = getc(fp)) != '\n' && c != EOF )
				;
		if ( sscanf(line, fmt, name, value ) == 2 && *name != '#' )
			return 1;
	}
	return EOF;
}
	


/* ------------------------------------------------------ *
   process_rq( char *rq, FILE *fpout)
   do what the request asks for and write reply to fp
   rq is HTTP command:  GET /foo/bar.html HTTP/1.0
   ------------------------------------------------------ */

void process_rq(char *rq, FILE *fp)
{
	char	cmd[MAX_RQ_LEN], arg[MAX_RQ_LEN];
	char	*item, *modify_argument();

	if ( sscanf(rq, "%s%s", cmd, arg) != 2 ){
		bad_request(fp);
		return;
	}

	item = modify_argument(arg, MAX_RQ_LEN);
	if ( strcmp(cmd,"GET") != 0 )
		cannot_do(fp);
	else if ( not_exist( item ) )
		do_404(item, fp );
	else if ( isadir( item ) )
		do_ls( item, fp );
	else if ( ends_in_cgi( item ) )
		do_exec( item, fp );
	else
		do_cat( item, fp );
}

/*
 * modify_argument
 *  purpose: many roles
 *		security - remove all ".." components in paths
 *		cleaning - if arg is "/" convert to "."
 *  returns: pointer to modified string
 *     args: array containing arg and length of that array
 */

char *
modify_argument(char *arg, int len)
{
	char	*nexttoken;
	char	*copy = malloc(len);

	if ( copy == NULL )
		oops("memory error", 1);

	/* remove all ".." components from path */
	/* by tokeninzing on "/" and rebuilding */
	/* the string without the ".." items	*/

	*copy = '\0';

	nexttoken = strtok(arg, "/");
	while( nexttoken != NULL )
	{
		if ( strcmp(nexttoken,"..") != 0 )
		{
			if ( *copy )
				strcat(copy, "/");
			strcat(copy, nexttoken);
		}
		nexttoken = strtok(NULL, "/");
	}
	strcpy(arg, copy);
	free(copy);

	/* the array is now cleaned up */
	/* handle a special case       */

	if ( strcmp(arg,"") == 0 )
		strcpy(arg, ".");
	return arg;
}
/* ------------------------------------------------------ *
   the reply header thing: all functions need one
   if content_type is NULL then don't send content type
   ------------------------------------------------------ */

void
header( FILE *fp, int code, char *msg, char *content_type )
{
	fprintf(fp, "HTTP/1.0 %d %s\r\n", code, msg);

    time_t t; 
    time(&t);
    fprintf(fp, "Date: %s", ctime(&t) );
    
    char* web_server_name = full_hostname();
    char arr[ strlen( web_server_name ) + 1 ];    
    for (int i = 0; i < strlen( web_server_name ); i++)
        arr[i] = web_server_name[i];
    arr[strlen( web_server_name )] = '\0';
    fprintf(fp, "Name: %s\r\n", arr);
    
    fprintf(fp, "Version: %s\r\n", VERSION);
	
    if ( content_type )
		fprintf(fp, "Content-Type: %s\r\n", content_type );
}

/* ------------------------------------------------------ *
   simple functions first:
	bad_request(fp)     bad request syntax
        cannot_do(fp)       unimplemented HTTP command
    and do_404(item,fp)     no such object
   ------------------------------------------------------ */

void
bad_request(FILE *fp)
{
	header(fp, 400, "Bad Request", "text/plain");
	fprintf(fp, "\r\nI cannot understand your request\r\n");
}

void
cannot_do(FILE *fp)
{
	header(fp, 501, "Not Implemented", "text/plain");
	fprintf(fp, "\r\n");

	fprintf(fp, "That command is not yet implemented\r\n");
}

void
do_404(char *item, FILE *fp)
{
	header(fp, 404, "Not Found", "text/plain");
	fprintf(fp, "\r\n");

	fprintf(fp, "The item you requested: %s\r\nis not found\r\n", 
			item);
}

/* ------------------------------------------------------ *
   the directory listing section
   isadir() uses stat, not_exist() uses stat
   do_ls runs ls. It should not
   ------------------------------------------------------ */

int
isadir(char *f)
{
	struct stat info;
	return ( stat(f, &info) != -1 && S_ISDIR(info.st_mode) );
}

int
not_exist(char *f)
{
	struct stat info;

	return( stat(f,&info) == -1 && errno == ENOENT );
}

/*
 * lists the directory named by 'dir' 
 * sends the listing to the stream at fp
 */
void
do_ls(char *dir, FILE *fp)
{
	int	fd;	/* file descriptor of stream */

	header(fp, 200, "OK", "text/plain");
	fprintf(fp,"\r\n");
	fflush(fp);

	fd = fileno(fp);
	dup2(fd,1);
	dup2(fd,2);
	execlp("/bin/ls","ls","-l",dir,NULL);
	perror(dir);
}

/* ------------------------------------------------------ *
   the cgi stuff.  function to check extension and
   one to run the program.
   ------------------------------------------------------ */

char *
file_type(char *f)
/* returns 'extension' of file */
{
	char	*cp;
	if ( (cp = strrchr(f, '.' )) != NULL )
		return cp+1;
	return "";
}

int
ends_in_cgi(char *f)
{
	return ( strcmp( file_type(f), "cgi" ) == 0 );
}

void
do_exec( char *prog, FILE *fp)
{
	int	fd = fileno(fp);

	header(fp, 200, "OK", NULL);
	fflush(fp);

	dup2(fd, 1);
	dup2(fd, 2);
	execl(prog,prog,NULL);
	perror(prog);
}
/* ------------------------------------------------------ *
   do_cat(filename,fp)
   sends back contents after a header
   ------------------------------------------------------ */

void
do_cat(char *f, FILE *fpsock)
{
	char	*extension = file_type(f);
	char	*content = "text/plain";
	FILE	*fpfile;
	int	c;

    for ( int i = 0; content_types[i].extension != NULL; i++ )
    {
        if ( strcmp( extension, content_types[i].extension ) == 0 ) {
            content = content_types[i].content;
            break;
        }        
    }

	fpfile = fopen( f , "r");
	if ( fpfile != NULL )
	{
		header( fpsock, 200, "OK", content );
		fprintf(fpsock, "\r\n");
		while( (c = getc(fpfile) ) != EOF )
			putc(c, fpsock);
		fclose(fpfile);
	}
}

char *
full_hostname()
/*
 * returns full `official' hostname for current machine
 * NOTE: this returns a ptr to a static buffer that is
 *       overwritten with each call. ( you know what to do.)
 */
{
	struct	hostent		*hp;
	char	hname[MAXHOSTNAMELEN];
	static  char fullname[MAXHOSTNAMELEN];

	if ( gethostname(hname,MAXHOSTNAMELEN) == -1 )	/* get rel name	*/
	{
		perror("gethostname");
		exit(1);
	}
	hp = gethostbyname( hname );		/* get info about host	*/
	if ( hp == NULL )			/*   or die		*/
		return NULL;
	strcpy( fullname, hp->h_name );		/* store foo.bar.com	*/
	return fullname;			/* and return it	*/
}


void fatal(char *fmt, char *str)
{
	fprintf(stderr, fmt, str);
	exit(1);
}

void done(int n)
{
	if ( mysocket != -1 ){
		fprintf(stderr, "closing socket\n");
		close(mysocket);
	}
	exit(0);
}
