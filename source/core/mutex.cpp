#include "shared.h"
#include "mutex.h"

#include "time.h"

static constexpr const char *kMutexDefaultName = "UNAMED MUTEX";

TestDriveMutex::TestDriveMutex( const char *pMutexName /*= nullptr */ )
    : name( kMutexDefaultName )
#if defined( OTDU_WIN32 )
      ,
      criticalSection(),
      lockedByThreadID( 0 )
#elif defined( OTDU_UNIX )
      ,
      mutex( PTHREAD_MUTEX_INITIALIZER )
#endif
      ,
      lockCount( 0 ),
      totalLockDurationInSeconds( 0.0f ),
      lockStartTimeInSeconds( 0.0f ),
      bLocked( 0 ),
      bInitialized( 0 ),
      bMarkedForDeletion( 0 ) {
    initialize( pMutexName );
}

TestDriveMutex::~TestDriveMutex() {}

void TestDriveMutex::initialize( const char *pMutexName /*= nullptr */ ) {
    if ( bInitialized ) {
        return;
    }
#if defined( OTDU_WIN32 )
    InitializeCriticalSection( &criticalSection );
#elif defined( OTDU_UNIX )
    pthread_mutexattr_t attr;
    pthread_mutexattr_init( &attr );
    pthread_mutexattr_settype( &attr, PTHREAD_MUTEX_RECURSIVE );
    pthread_mutex_init( &mutex, &attr );
    pthread_mutexattr_destroy( &attr );
#endif
    if ( pMutexName ) {
        name = std::string( pMutexName );
    }

    bInitialized = true;
}

void TestDriveMutex::terminate() {
    if ( bMarkedForDeletion ) {
        return;
    }

    bMarkedForDeletion = true;
#if defined( OTDU_WIN32 )
    DeleteCriticalSection( &criticalSection );
#elif defined( OTDU_UNIX )
    pthread_mutex_destroy( &mutex );
#endif
}

void TestDriveMutex::lock() {
#if defined( OTDU_WIN32 )
    EnterCriticalSection( &criticalSection );
    lockedByThreadID = GetCurrentThreadId();
#elif defined( OTDU_UNIX )
    pthread_mutex_lock( &mutex );
#endif
    lockCount++;
    bLocked = true;
    lockStartTimeInSeconds = GetTimeSeconds();
}

void TestDriveMutex::unlock() {
    float startTime = lockStartTimeInSeconds;
    float currentTime = GetTimeSeconds();

    lockStartTimeInSeconds = 0.0f;
    totalLockDurationInSeconds += ( currentTime - startTime );

    if ( lockCount > 0 ) {
        lockCount--;
    }
    const bool bFullyReleased = ( lockCount == 0 );
    bLocked = !bFullyReleased;

#if defined( OTDU_WIN32 )
    if ( bFullyReleased ) {
        lockedByThreadID = 0;
    }
    LeaveCriticalSection( &criticalSection );
#elif defined( OTDU_UNIX )
    pthread_mutex_unlock( &mutex );
#endif
}
