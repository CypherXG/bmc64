//
// plus4emulatorcore.h
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef plus4emualtorcore_h
#define plus4emulatorcore_h

#include <circle/memory.h>
#include <circle/multicore.h>
#include <circle/spinlock.h>
#include "emulatorcore.h"

extern "C" {
#include "third_party/common/circle.h"
}

class Plus4EmulatorCore
 : public EmulatorCore
#ifdef ARM_ALLOW_MULTI_CORE
   ,public CMultiCoreSupport
#endif
{
public:
  Plus4EmulatorCore(CMemorySystem *pMemorySystem);
  ~Plus4EmulatorCore(void);

  void Run(unsigned nCore)
#ifdef ARM_ALLOW_MULTI_CORE
      override
#endif
  ;

#ifndef ARM_ALLOW_MULTI_CORE
  bool Initialize() { return true; }
#endif

  bool Init(void) override;
  void LaunchEmulator(char *timing_option) override;

private:
  bool launch_;
  char timing_option_[8];
  CSpinLock m_Lock;

  void RunMainPlus4(bool wait);
  void ComputeResidFilter(int model);
};

#endif
