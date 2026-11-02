// SPDX-FileCopyrightText: 2019 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QRegularExpression>

#include "vendor/ztroubleshoot.h"

class ut_TroubleShoot : public testing::Test
{
protected:
    void SetUp() override;
    void TearDown() override;
    TroubleShoot *m_troubleShoot;
};

void ut_TroubleShoot::SetUp()
{
    m_troubleShoot = new TroubleShoot("5150");
}

void ut_TroubleShoot::TearDown()
{
    delete m_troubleShoot;
    m_troubleShoot = nullptr;
}

// The task is not started: running it needs a CUPS server and a real queue.
TEST_F(ut_TroubleShoot, defaultJobs)
{
    auto ret = m_troubleShoot->getJobs();
    EXPECT_EQ(ret.size(), 3);
}


