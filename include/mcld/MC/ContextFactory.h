//===- ContextFactory.h ---------------------------------------------------===//
//
//                     The MCLinker Project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#ifndef MCLD_CONTEXT_FACTORY_H
#define MCLD_CONTEXT_FACTORY_H
#ifdef ENABLE_UNITTEST
#include <gtest.h>
#endif

#include <mcld/LD/LDContext.h>
#include <mcld/Support/UniqueGCFactory.h>
#include <mcld/Support/Path.h>

namespace mcld
{
/** \class ContextFactory
 *  \brief ContextFactory avoids the duplicated LDContext of the same file.
 *
 *  MCLinker is designed for efficient memory usage. Because user can give
 *  MCLinker the same input file many times on the command line, MCLinker must
 *  avoid opening identical file twice.
 *
 *  ContextFactory is the guard to prevent redundant opening. MCLinker does not
 *  create LDContext directly. Instead, it creates LDContext by ContextFactory.
 *  ContextFactory returns the identical reference of LDContext if it's openend.
 *
 *  @see LDContext
 *  @see UniqueGCFactoryBase
 */
class ContextFactory : public UniqueGCFactoryBase<sys::fs::Path, LDContext, 0>
{
public:
  explicit ContextFactory(size_t pNum);
  ~ContextFactory();

  LDContext* produce(const sys::fs::Path& pPath);
};

} // namespace of mcld

#endif

