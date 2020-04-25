#include	<stdio.h>
#include	<time.h>
#include	<stdlib.h>

/*
 * 	function 	rfc822_time()
 *	purpose		return a string suitable for web servers
 *	details		Sun, 06 Nov 1994 08:49:37 GMT    
 *	method		use gmtime() to get struct
 *			then use strftime() to format data to spec
 *	arg		a time_t value 
 *	returns		a pointer to a static buffer (be careful)
 */

char *
rfc822_time(time_t thetime)
{
	struct tm *t ;
	static  char retval[36];
	
	t = gmtime( &thetime );		/* break into parts	*/
					/* format to spec	*/
	strftime(retval, 36, "%a, %d %b %Y %H:%M:%S GMT", t);
	return retval;
}

#ifdef STANDALONE
int main()
{
	printf ( "[%s]\n", rfc822_time( time(0L) ) );
	return 0;
}
#endif
