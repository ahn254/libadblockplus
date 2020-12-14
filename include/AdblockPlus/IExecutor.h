/*
 * This file is part of Adblock Plus <https://adblockplus.org/>,
 * Copyright (C) 2006-present eyeo GmbH
 *
 * Adblock Plus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Adblock Plus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Adblock Plus.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ADBLOCK_PLUS_IEXECUTOR_H
#define ADBLOCK_PLUS_IEXECUTOR_H

#include <functional>

namespace AdblockPlus
{
  class IExecutor
  {
  public:
    virtual ~IExecutor() = default;

    /**
     * Executes the given task at some time in the future. Task will be never executed after Stop is
     * called.
     */
    virtual void Dispatch(const std::function<void()>& task) = 0;

    /**
     * Stop accepting tasks.
     */
    virtual void Stop() = 0;
  };
}

#endif // ADBLOCK_PLUS_IEXECUTOR_H