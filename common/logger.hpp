//----------------------------------------------------------------------------
//
// File:	Logger.hpp
// Name:	Error Logger object header
// Programmer:	Marc Rousseau			 
// Date:	15 April 1998
// Description:
//
// Revision History:
//
//----------------------------------------------------------------------------

#ifndef _LOGGER_HPP_
#define _LOGGER_HPP_

#ifdef ERROR
    #undef ERROR
#endif
#ifdef ASSERT
    #undef ASSERT
#endif

#if defined ( DEBUG )

    struct LOG_FLAGS {
        bool    FunctionEntry;
    };

    extern LOG_FLAGS g_LogFlags;

    #define FUNCTION_ENTRY(t,f,l)					\
        char l_buffer [64];						\
        void *l_pThis = ( void * ) t;					\
        char *l_FnName = f;						\
        dbgString l_prefix ( l_buffer );				\
        if ( l_pThis ) l_prefix << ( void * ) l_pThis << " - ";		\
        l_prefix << l_FnName;						\
        if ( l && g_LogFlags.FunctionEntry ) {				\
            DBG_MINOR_TRACE2 (( const char * ) l_prefix );		\
        }								\
        l_prefix << " - ";						\
								
    #define ASSERT(x)	(( ! ( x )) ? dbg_Assert (  dbg_FileName, __LINE__, dbg_Stream () << (( const char * ) l_prefix ) << #x ), 1 : 0 )
    										     	 
#else

    #define FUNCTION_ENTRY(t,f,l)
    #define ASSERT(x)
    
#endif

#define TRACE(x) {							\
        DBG_MAJOR_TRACE2 (( const char * ) l_prefix << x );		\
    }

#define MINOR_EVENT(x) {						\
        DBG_MINOR_EVENT2 (( const char * ) l_prefix << x );		\
    }

#define MAJOR_EVENT(x) {						\
        DBG_MAJOR_EVENT2 (( const char * ) l_prefix << x );		\
    }

#define EVENT   MINOR_EVENT
    
#define STATUS(x) {							\
        DBG_STATUS2 (( const char * ) l_prefix << x );			\
    }

#define WARNING(x) {							\
        DBG_WARNING2 (( const char * ) l_prefix << x );			\
    }

#define ERROR(x) {							\
        DBG_ERROR2 (( const char * ) l_prefix << x );			\
    }

#define FATAL(x) {							\
        DBG_FATAL2 (( const char * ) l_prefix << x );			\
    }

#if ! defined ( DEBUG )

    #define DBG_REGISTER(x)
    #define DBG_STRING(x)	""
    #define DBG_MINOR_TRACE(x)
    #define DBG_MINOR_TRACE2(x)
    #define DBG_MAJOR_TRACE(x)
    #define DBG_MAJOR_TRACE2(x)
    #define DBG_MINOR_EVENT(x)
    #define DBG_MINOR_EVENT2(x)
    #define DBG_MAJOR_EVENT(x)
    #define DBG_MAJOR_EVENT2(x)
    #define DBG_STATUS(x)
    #define DBG_STATUS2(x)
    #define DBG_WARNING(x)
    #define DBG_WARNING2(x)
    #define DBG_ERROR(x)
    #define DBG_ERROR2(x)
    #define DBG_FATAL(x)
    #define DBG_FATAL2(x)
    #define DBG_ASSERT(x)	0

#else

    #define DBG_REGISTER(x)	static ULONG dbg_FileName = dbg_RegisterFile ( x );	
    #define DBG_STRING(x)	x
    #define DBG_MINOR_TRACE(x)	dbg_Record ( _MINOR_TRACE_, dbg_FileName, __LINE__, x )
    #define DBG_MINOR_TRACE2(x)	dbg_Record ( _MINOR_TRACE_, dbg_FileName, __LINE__, dbg_Stream () << x ), dbg_Stream ().flush ()
    #define DBG_MAJOR_TRACE(x)	dbg_Record ( _MAJOR_TRACE_, dbg_FileName, __LINE__, x )
    #define DBG_MAJOR_TRACE2(x)	dbg_Record ( _MAJOR_TRACE_, dbg_FileName, __LINE__, dbg_Stream () << x ), dbg_Stream ().flush ()
    #define DBG_MINOR_EVENT(x)	dbg_Record ( _MINOR_EVENT_, dbg_FileName, __LINE__, x )
    #define DBG_MINOR_EVENT2(x)	dbg_Record ( _MINOR_EVENT_, dbg_FileName, __LINE__, dbg_Stream () << x ), dbg_Stream ().flush ()
    #define DBG_MAJOR_EVENT(x)	dbg_Record ( _MAJOR_EVENT_, dbg_FileName, __LINE__, x )
    #define DBG_MAJOR_EVENT2(x)	dbg_Record ( _MAJOR_EVENT_, dbg_FileName, __LINE__, dbg_Stream () << x ), dbg_Stream ().flush ()
    #define DBG_STATUS(x)	dbg_Record ( _STATUS_,      dbg_FileName, __LINE__, x )
    #define DBG_STATUS2(x)	dbg_Record ( _STATUS_,      dbg_FileName, __LINE__, dbg_Stream () << x ), dbg_Stream ().flush ()
    #define DBG_WARNING(x)	dbg_Record ( _WARNING_,     dbg_FileName, __LINE__, x )
    #define DBG_WARNING2(x)	dbg_Record ( _WARNING_,     dbg_FileName, __LINE__, dbg_Stream () << x ), dbg_Stream ().flush ()
    #define DBG_ERROR(x)	dbg_Record ( _ERROR_,       dbg_FileName, __LINE__, x )
    #define DBG_ERROR2(x)	dbg_Record ( _ERROR_,       dbg_FileName, __LINE__, dbg_Stream () << x ), dbg_Stream ().flush ()
    #define DBG_FATAL(x)	dbg_Record ( _FATAL_,       dbg_FileName, __LINE__, x )
    #define DBG_FATAL2(x)	dbg_Record ( _FATAL_,       dbg_FileName, __LINE__, dbg_Stream () << x ), dbg_Stream ().flush ()
    #define DBG_ASSERT(x)	(( ! ( x )) ? dbg_Assert (  dbg_FileName, __LINE__, #x ), 1 : 0 )

enum eLOG_TYPE {
    _MINOR_TRACE_,
    _MAJOR_TRACE_,
    _MINOR_EVENT_,
    _MAJOR_EVENT_,
    _STATUS_,
    _WARNING_,
    _ERROR_,
    _FATAL_,
    LOG_TYPE_MAX
};  

#pragma option -a1
struct sLogEntry {
    ULONG       Type;
    ULONG       FileIndex;
    ULONG       LineNumber;
    ULONG       ThreadID;
    __int64     Time;
    char        Text [ 256 ];
};
#pragma option -a.

#pragma option -pr

class dbgString {

    friend dbgString &hex ( dbgString & );
    friend dbgString &dec ( dbgString & );

    int     m_Base;
    bool    m_OwnBuffer;
    char   *m_Ptr;
    char   *m_Buffer;

public:

    dbgString ( char * );
    
    operator const char * ();
    void flush ();

    dbgString &operator << ( char );
    dbgString &operator << ( const char * );
    dbgString &operator << ( int );
    dbgString &operator << ( long );
    dbgString &operator << ( unsigned long );
    dbgString &operator << ( __int64 );
    dbgString &operator << ( unsigned __int64 );
    dbgString &operator << ( void * );
    dbgString &operator << ( dbgString &(*f) ( dbgString & ));

};

dbgString &hex ( dbgString & );
dbgString &dec ( dbgString & );

extern "C" {

    // Client Functions
    dbgString  &dbg_Stream ();
    ULONG       dbg_RegisterFile ( const char * );
    void        dbg_Assert ( int, int, const char * );
    void        dbg_Record ( int, int, int, const char * );

    // Server Functions
    bool        dbg_StartServer ();
    bool        dbg_StopServer ();
    sLogEntry  *dbg_GetRecord ();
    const char *dbg_GetFileName ( ULONG );
    const char *dbg_GetModuleName ( ULONG );

};

#endif

#endif
