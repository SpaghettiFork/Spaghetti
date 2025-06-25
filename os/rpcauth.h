#ifndef _XSERVER_OS_RPCAUTH_H
#define _XSERVER_OS_RPCAUTH_H

#include "auth.h"

extern void SecureRPCInit(AuthInitArgs);
extern XID SecureRPCCheck(AuthCheckArgs);
extern int SecureRPCAdd(AuthAddCArgs);
extern int SecureRPCFromID(AuthFromIDArgs);
extern int SecureRPCRemove(AuthRemCArgs);
extern int SecureRPCReset(AuthRstCArgs);

#endif /* _XSERVER_OS_RPCAUTH_H */