#pragma once

/* Only the persistent capture work item calls this state machine.  It uses
 * the existing platform client, never opens a second CCI or power owner. */
typedef struct _NABU_REAR_AF_STATE {
    ULONG Stage; /* 1 AE warmup, 2 coarse, 3 fine, 4 return, 5 hold, 6 error */
    ULONG Frames;
    ULONG StableAeFrames;
    LONG AppliedGeneration;
    BOOLEAN PositionKnown;
    USHORT Position;
    USHORT Target;
    USHORT FineStart;
    USHORT FineEnd;
    ULONG SkipFrames;
    ULONG SampleCount;
    ULONG Samples[3];
    ULONG CoarseIndex;
    ULONG LowestScore;
    ULONG HoldScore;
    ULONG HoldSignal;
    ULONG IdleFrames;
    ULONG LostCount;
    BOOLEAN SweepEvidence;
    ULONGLONG SettleUntil;
    ULONGLONG RetryAfter;
} NABU_REAR_AF_STATE;

static ULONG
NabuOwnAfMedian3(_In_reads_(3) const ULONG *Values)
{
    ULONG a = Values[0], b = Values[1], c = Values[2];
    return max(min(a, b), min(max(a, b), c));
}

static ULONG
NabuOwnAfSignal(_In_reads_bytes_(NABU_REAR_FRAME_BYTES) const UCHAR *Raw)
{
    const USHORT *pixels = (const USHORT *)Raw;
    ULONGLONG sum = 0;
    ULONG count = 0, x, y, mean;
    for (y = 440; y < 1120; y += 32) {
        for (x = 440; x < 1660; x += 32) {
            sum += pixels[y * NABU_REAR_WIDTH + x];
            ++count;
        }
    }
    mean = count ? (ULONG)(sum / count) : 64UL;
    return mean > 64UL ? mean - 64UL : 0UL;
}

static VOID
NabuOwnAfFail(_Inout_ PNABU_OWN_STAGE23_CONTEXT Context,
              _Inout_ NABU_REAR_AF_STATE *Af, _In_ NTSTATUS Status)
{
    Af->Stage = 6;
    Context->AfProbePhase = 5;
    Context->AfProbeStatus = (ULONG)Status;
    Context->AfFocusConfidence = 0;
    Af->SweepEvidence = FALSE;
    if (Status != STATUS_CANCELLED) Af->PositionKnown = FALSE;
    InterlockedExchange(&Context->AutoExposureFreeze, 0);
}

static VOID
NabuOwnAfBeginSweep(_Inout_ PNABU_OWN_STAGE23_CONTEXT Context,
                    _Inout_ NABU_REAR_AF_STATE *Af)
{
    Af->Stage = 2;
    Af->Target = 241;
    Af->SkipFrames = 3;
    Af->SampleCount = 0;
    Af->CoarseIndex = 0;
    Af->LowestScore = MAXULONG;
    Af->LostCount = 0;
    Af->SweepEvidence = FALSE;
    Context->AfSweepBestScore = 0;
    Context->AfSweepBestPosition = 241;
    Context->AfFocusConfidence = 0;
    Context->AfProbePhase = 2;
    ++Context->AfAutomaticRuns;
    RtlZeroMemory(Context->AfSweepScore, sizeof(Context->AfSweepScore));
    InterlockedExchange(&Context->AutoExposureFreeze, 1);
}

static VOID
NabuOwnAfTick(_Inout_ PNABU_OWN_STAGE23_CONTEXT Context,
              _In_ PPLATFORM_CLIENT Client,
              _Inout_ NABU_REAR_AF_STATE *Af,
              _In_reads_bytes_(NABU_REAR_FRAME_BYTES) const UCHAR *Raw)
{
    ULONGLONG now = KeQueryInterruptTime();
    LONG applied = InterlockedCompareExchange(
        &Context->AutoExposureAppliedGeneration, 0, 0);
    LONG requested = InterlockedCompareExchange(
        &Context->AutoExposureRequestedGeneration, 0, 0);
    NTSTATUS status;
    ULONG score;

    if (Af->Stage == 0 || Af->Stage == 6) return;
    if (InterlockedCompareExchange(
            &Context->PersistentStopRequested, 0, 0) != 0) {
        NabuOwnAfFail(Context, Af, STATUS_CANCELLED);
        return;
    }
    ++Af->Frames;
    if (requested == applied && applied == Af->AppliedGeneration) {
        if (Af->StableAeFrames < 90) ++Af->StableAeFrames;
    } else Af->StableAeFrames = 0;
    Af->AppliedGeneration = applied;

    if (Af->Stage == 1) {
        /* Three sensor-latency cycles after cold AE, then six unchanged
         * applied frames.  A bounded fallback handles saturated dark scenes. */
        if (Af->Frames < 24 ||
            (Af->StableAeFrames < 6 && Af->Frames < 90)) return;
        if (requested != applied || !NT_SUCCESS((NTSTATUS)
                InterlockedCompareExchange(&Context->AutoExposureApplyStatus, 0, 0))) return;
        if (Context->RealChipId != 0x0D42UL ||
            Context->RealVafGpioReadValid == 0 ||
            (Context->RealBobEn & 0x80UL) == 0 || !NabuOwnAfPadReady()) {
            NabuOwnAfFail(Context, Af, STATUS_DEVICE_NOT_READY);
            return;
        }
        InterlockedExchange(&Context->AutoExposureFreeze, 1);
        Context->AfProbePhase = 1;
        status = NabuOwnAfInit(Context, Client);
        if (!NT_SUCCESS(status)) {
            NabuOwnAfFail(Context, Af, status);
            return;
        }
        Af->PositionKnown = TRUE;
        Af->Position = 241;
        Context->AfProbeFinalPosition = 241;
        Context->AfProbeStatus = (ULONG)STATUS_SUCCESS;
        NabuOwnAfBeginSweep(Context, Af);
        Af->SettleUntil = KeQueryInterruptTime() + 800000ULL;
        return;
    }

    if (Af->Stage == 5) {
        ULONG signal, normalized;
        if (++Af->IdleFrames < 15 || now < Af->RetryAfter ||
            Af->StableAeFrames < 6) return;
        Af->IdleFrames = 0;
        signal = NabuOwnAfSignal(Raw);
        /* Do not refocus on low-signal noise.  Normalize detail for changes
         * in scene illumination before requiring three sustained losses. */
        if (signal < 20) {
            Af->LostCount = 0;
            return;
        }
        if (Af->HoldSignal < 20) {
            /* A held low-signal baseline cannot normalize later detail.
             * Retry only after three eligible strong-signal observations. */
            ++Af->LostCount;
        } else {
            normalized = (ULONG)(((ULONGLONG)NabuOwnAfSharpness(Raw) *
                                   Af->HoldSignal) / signal);
            if ((Context->AfFocusConfidence != 0 &&
                 (ULONGLONG)normalized * 100ULL < (ULONGLONG)Af->HoldScore * 55ULL) ||
                (Context->AfFocusConfidence == 0 &&
                 (ULONGLONG)normalized > (ULONGLONG)Af->HoldScore * 2ULL)) {
                ++Af->LostCount;
            } else Af->LostCount = 0;
        }
        if (Af->LostCount >= 3) {
            NabuOwnAfBeginSweep(Context, Af);
            Af->SettleUntil = now;
        }
        return;
    }

    if (now < Af->SettleUntil) return;
    if (Af->Position != Af->Target) {
        USHORT delta = Af->Position > Af->Target ?
            (USHORT)min(Af->Position - Af->Target, 30) :
            (USHORT)min(Af->Target - Af->Position, 30);
        USHORT next = Af->Position > Af->Target ?
            (USHORT)(Af->Position - delta) : (USHORT)(Af->Position + delta);
        if (next < 241 || next > 511) {
            NabuOwnAfFail(Context, Af, STATUS_INVALID_PARAMETER);
            return;
        }
        status = NabuOwnAfPosition(Client, next);
        if (!NT_SUCCESS(status)) {
            NabuOwnAfFail(Context, Af, status);
            return;
        }
        Af->Position = next;
        Context->AfProbeFinalPosition = next;
        Af->SettleUntil = now + 600000ULL;
        Af->SkipFrames = 3;
        Af->SampleCount = 0;
        return;
    }
    if (Af->SkipFrames != 0) { --Af->SkipFrames; return; }

    if (Af->Stage == 4) {
        Af->Stage = 5;
        Af->HoldScore = NabuOwnAfSharpness(Raw);
        Af->HoldSignal = NabuOwnAfSignal(Raw);
        Af->RetryAfter = now + 100000000ULL;
        Af->IdleFrames = 0;
        Context->AfProbePhase = 4;
        Context->AfProbeStatus = (ULONG)STATUS_SUCCESS;
        /* Coarse contrast is provisional until the selected lens
         * position has been applied and the return frames settled. */
        Context->AfFocusConfidence = Af->SweepEvidence ? 1UL : 0UL;
        if (Context->AfFocusConfidence != 0)
            Context->AfLastGoodPosition = Af->Position;
        InterlockedExchange(&Context->AutoExposureFreeze, 0);
        return;
    }

    Af->Samples[Af->SampleCount++] = NabuOwnAfSharpness(Raw);
    if (Af->SampleCount < 3) return;
    score = NabuOwnAfMedian3(Af->Samples);
    Af->SampleCount = 0;
    if (score > Context->AfSweepBestScore) {
        Context->AfSweepBestScore = score;
        Context->AfSweepBestPosition = Af->Position;
    }
    if (Af->Stage == 2) {
        Context->AfSweepIndex = Af->CoarseIndex;
        Context->AfSweepScore[Af->CoarseIndex] = score;
        if (Af->CoarseIndex == 0) Context->AfProbeBaselineScore = score;
        if (Af->CoarseIndex == 1) Context->AfProbeMovedScore = score;
        Af->LowestScore = min(Af->LowestScore, score);
        if (++Af->CoarseIndex < 10) {
            Af->Target = (USHORT)(241 + 30 * Af->CoarseIndex);
            return;
        }
        if (Context->AfSweepBestScore == 0 ||
            (ULONGLONG)Context->AfSweepBestScore * 100ULL <
                (ULONGLONG)Af->LowestScore * 125ULL) {
            /* A flat noise curve carries no focus evidence. Keep the last
             * confident position, or the verified initial position. */
            Af->Target = Context->AfLastGoodPosition >= 241 &&
                         Context->AfLastGoodPosition <= 511 ?
                         (USHORT)Context->AfLastGoodPosition : 241;
            Af->Stage = 4;
            return;
        }
        Af->SweepEvidence = TRUE;
        Af->FineStart = (USHORT)max(241, (LONG)Context->AfSweepBestPosition - 20);
        Af->FineEnd = (USHORT)min(511, Context->AfSweepBestPosition + 20);
        Af->Target = Af->FineStart;
        Af->Stage = 3;
        Context->AfProbePhase = 3;
        return;
    }
    if (Af->Target < Af->FineEnd) {
        Af->Target = (USHORT)min(Af->Target + 10, Af->FineEnd);
    } else {
        Af->Target = (USHORT)Context->AfSweepBestPosition;
        Af->Stage = 4;
    }
}

/* A completed-session observation is not current focus after lens parking.
 * Keep the last good search position for fallback, clear active confidence,
 * and do not retain known-position evidence after a failed park command. */
static VOID
NabuOwnAfEndSession(_Inout_ PNABU_OWN_STAGE23_CONTEXT Context,
                    _Inout_ NABU_REAR_AF_STATE *Af, _In_ NTSTATUS ParkStatus)
{
    Context->AfFocusConfidence = 0;
    Af->SweepEvidence = FALSE;
    if (!NT_SUCCESS(ParkStatus)) {
        Af->PositionKnown = FALSE;
        Context->AfProbePhase = 6;
    }
    if (Af->PositionKnown) Context->AfProbeFinalPosition = Af->Position;
    InterlockedExchange(&Context->AutoExposureFreeze, 0);
}
