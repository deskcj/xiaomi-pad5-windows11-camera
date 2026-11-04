#ifndef NABU_REAR_INTEGRATION_ENCODING_H
#define NABU_REAR_INTEGRATION_ENCODING_H

/* Direct ordinary scalar encoding verified from original ARM64 OV13B10.
 * These are command-code units, not qualified optical lines/nanoseconds. */
#define NABU_REAR_INTEGRATION_ENCODING_VERSION 2UL
#define NABU_REAR_INTEGRATION_UNITS_DIRECT_CODE 1UL
#define NABU_REAR_INTEGRATION_MIN_CODE 4UL
#define NABU_REAR_INTEGRATION_MAX_CODE 2336UL
#define NABU_REAR_INTEGRATION_INITIAL_CODE 1024UL

/* Header is pure and uses only C scalar types. No register or CCI access. */
static __inline int
NabuRearEncodeOrdinaryIntegration(unsigned long Code, unsigned char Bytes[3])
{
    if (Bytes == 0 || Code < NABU_REAR_INTEGRATION_MIN_CODE ||
        Code > NABU_REAR_INTEGRATION_MAX_CODE) return 0;
    Bytes[0] = (unsigned char)((Code >> 16) & 0x0fUL);
    Bytes[1] = (unsigned char)((Code >> 8) & 0xffUL);
    Bytes[2] = (unsigned char)(Code & 0xffUL);
    return 1;
}

/* A zero-version, fresh driver context starts1024, the same emitted bytes
 * as baseline06164<<4. Other versions are discarded, never migrated by16.
 * Valid new-unit state remains available across later opens only with prior
 * complete command/session evidence. This does not assert optical settling. */
static __inline unsigned long
NabuRearSelectIntegrationStart(unsigned long Version, unsigned long Code,
                              int TrustedApplied, int *WarmStart)
{
    unsigned char bytes[3];
    int valid = TrustedApplied &&
        Version == NABU_REAR_INTEGRATION_ENCODING_VERSION &&
        NabuRearEncodeOrdinaryIntegration(Code, bytes);
    if (WarmStart != 0) *WarmStart = valid;
    return valid ? Code : NABU_REAR_INTEGRATION_INITIAL_CODE;
}

/* Prior successful command/session evidence; no optical-settled claim. */
static __inline int
NabuRearPriorIntegrationTrusted(long RequestedGeneration, long AppliedGeneration,
                               long ApplyStatus, long SessionStatus,
                               long PublishedFrames, long TerminalStatus)
{
    return RequestedGeneration > 0 && RequestedGeneration == AppliedGeneration &&
        ApplyStatus == 0 && SessionStatus == 0 && PublishedFrames > 0 &&
        TerminalStatus == 0;
}
#endif
