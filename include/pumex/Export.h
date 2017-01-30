#pragma once

// disable VisualStudio warnings
#if defined(_MSC_VER)
  #pragma warning( disable : 4244 )
  #pragma warning( disable : 4251 )
  #pragma warning( disable : 4275 )
  #pragma warning( disable : 4512 )
  #pragma warning( disable : 4267 )
  #pragma warning( disable : 4702 )
  #pragma warning( disable : 4511 )
#endif


#ifdef PUMEX_EXPORTS
    #ifdef WIN32
        #define PUMEX_EXPORT  __declspec(dllexport)
    #else
        #define PUMEX_EXPORT
    #endif
    #define PUMEX_TEMPLATE
#else
    #ifdef WIN32
        #define PUMEX_EXPORT  __declspec(dllimport)
    #else
        #define PUMEX_EXPORT
    #endif
    #define PUMEX_TEMPLATE extern
#endif
