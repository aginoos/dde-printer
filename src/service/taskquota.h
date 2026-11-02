// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>

#include <sys/types.h>

// Admission control for the root service. Callers are counted by uid, so one
// local user cannot starve the others. A privileged request occupies a
// per-uid "pending" slot while polkit is asked (the authorization may wait for
// a person) and only takes one of the few global "running" slots once it has
// been granted. The unauthenticated log reader has its own, tighter limits.
class TaskQuota
{
public:
    enum class Kind { Privileged, LogRead };

    struct Limits {
        int privilegedPerUid = 2;     // pending + running privileged tasks of one uid
        int logReadPerUid = 1;        // concurrent log reads of one uid
        int pendingGlobal = 32;       // privileged tasks waiting for polkit, all uids
        int runningGlobal = 8;        // granted privileged tasks that are executing
        int logReadGlobal = 4;        // concurrent log reads, all uids
    };

    struct Ticket {
        uint uid = 0;
        Kind kind = Kind::Privileged;
        bool running = false;
        bool released = false;
    };

    TaskQuota() = default;
    explicit TaskQuota(const Limits &limits) : m_limits(limits) {}

    // Reserve a slot for a new request; false when a limit is reached.
    bool admit(uid_t uid, Kind kind, Ticket *ticket);
    // A privileged ticket whose authorization succeeded starts executing.
    bool startRunning(Ticket *ticket);
    // Idempotent.
    void release(Ticket *ticket);

    int pendingGlobal() const { return m_pending; }
    int runningGlobal() const { return m_running; }
    int logReadGlobal() const { return m_logReads; }
    int inFlightOf(uid_t uid, Kind kind) const;

private:
    Limits m_limits;
    QHash<uint, int> m_privileged;   // per uid
    QHash<uint, int> m_logReadsByUid;
    int m_pending = 0;
    int m_running = 0;
    int m_logReads = 0;
};
