// SPDX-FileCopyrightText: 2026 agino
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "taskquota.h"

namespace {

void decrement(QHash<uint, int> &counts, uint uid)
{
    const auto it = counts.find(uid);
    if (it == counts.end())
        return;
    if (--it.value() <= 0)
        counts.erase(it);
}

}

bool TaskQuota::admit(uid_t uid, Kind kind, Ticket *ticket)
{
    const uint key = static_cast<uint>(uid);
    if (kind == Kind::LogRead) {
        if (m_logReadsByUid.value(key) >= m_limits.logReadPerUid || m_logReads >= m_limits.logReadGlobal)
            return false;
        ++m_logReadsByUid[key];
        ++m_logReads;
    } else {
        if (m_privileged.value(key) >= m_limits.privilegedPerUid || m_pending >= m_limits.pendingGlobal)
            return false;
        ++m_privileged[key];
        ++m_pending;
    }
    if (ticket)
        *ticket = Ticket{key, kind, false, false};
    return true;
}

bool TaskQuota::startRunning(Ticket *ticket)
{
    if (!ticket || ticket->released || ticket->kind != Kind::Privileged || ticket->running)
        return false;
    if (m_running >= m_limits.runningGlobal)
        return false;
    --m_pending;
    ++m_running;
    ticket->running = true;
    return true;
}

void TaskQuota::release(Ticket *ticket)
{
    if (!ticket || ticket->released)
        return;
    ticket->released = true;
    if (ticket->kind == Kind::LogRead) {
        decrement(m_logReadsByUid, ticket->uid);
        --m_logReads;
        return;
    }
    decrement(m_privileged, ticket->uid);
    if (ticket->running)
        --m_running;
    else
        --m_pending;
}

int TaskQuota::inFlightOf(uid_t uid, Kind kind) const
{
    const uint key = static_cast<uint>(uid);
    return kind == Kind::LogRead ? m_logReadsByUid.value(key) : m_privileged.value(key);
}
