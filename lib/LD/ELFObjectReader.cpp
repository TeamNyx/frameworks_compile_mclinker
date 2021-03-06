//===- ELFObjectReader.cpp ------------------------------------------------===//
//
//                     The MCLinker Project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include <llvm/Support/ELF.h>
#include <llvm/ADT/Twine.h>
#include <llvm/Support/ErrorHandling.h>
#include <mcld/LD/ELFObjectReader.h>
#include <mcld/LD/ELFReader.h>
#include <mcld/MC/MCLDInput.h>
#include <mcld/MC/MCLinker.h>
#include <mcld/MC/MCRegionFragment.h>
#include <mcld/Target/GNULDBackend.h>

#include <string>
#include <cassert>

using namespace llvm;
using namespace mcld;

//==========================
// ELFObjectReader
/// constructor
ELFObjectReader::ELFObjectReader(GNULDBackend& pBackend, MCLinker& pLinker)
  : ObjectReader(),
    m_pELFReader(0),
    m_Linker(pLinker)
{
  if (32 == pBackend.bitclass() && pBackend.isLittleEndian()) {
    m_pELFReader = new ELFReader<32, true>(pBackend);
  }
}

/// destructor
ELFObjectReader::~ELFObjectReader()
{
  delete m_pELFReader;
}

/// isMyFormat
bool ELFObjectReader::isMyFormat(Input &pInput) const
{
  assert(pInput.hasMemArea());

  // Don't warning about the frequently requests.
  // MemoryArea has a list of cache to handle this.
  size_t hdr_size = m_pELFReader->getELFHeaderSize();
  MemoryRegion* region = pInput.memArea()->request(0, hdr_size);

  uint8_t* ELF_hdr = region->start();
  bool result = true;
  if (!m_pELFReader->isELF(ELF_hdr))
    result = false;
  else if (!m_pELFReader->isMyEndian(ELF_hdr))
    result = false;
  else if (!m_pELFReader->isMyMachine(ELF_hdr))
    result = false;
  else if (MCLDFile::Object != m_pELFReader->fileType(ELF_hdr))
    result = false;
  pInput.memArea()->release(region);
  return result;
}

/// readObject - read section header and create LDSections.
bool ELFObjectReader::readObject(Input& pInput)
{
  assert(pInput.hasMemArea());

  size_t hdr_size = m_pELFReader->getELFHeaderSize();
  MemoryRegion* region = pInput.memArea()->request(0, hdr_size);
  uint8_t* ELF_hdr = region->start();
  bool result = m_pELFReader->readSectionHeaders(pInput, m_Linker, ELF_hdr);
  pInput.memArea()->release(region);
  return result;
}

/// readSections - read all regular sections.
bool ELFObjectReader::readSections(Input& pInput)
{
  // handle sections
  LDContext::sect_iterator section, sectEnd = pInput.context()->sectEnd();
  for (section = pInput.context()->sectBegin(); section != sectEnd; ++section) {
    // ignore the section if the LDSection* in input context is NULL
    if (NULL == *section)
        continue;

    switch((*section)->kind()) {
      /** group sections **/
      case LDFileFormat::Group: {
        assert(NULL != (*section)->getLink());
        ResolveInfo* signature =
              m_pELFReader->readSymbol(pInput,
                                       *(*section)->getLink(),
                                       m_Linker.getLDInfo(),
                                       (*section)->getInfo());

        bool exist = false;
        signatures().insert(signature->name(), exist);

        if (exist) {
          // if this is not the first time we see this group signature, then
          // ignore all the members in this group (set NULL)
          MemoryRegion* region =
              pInput.memArea()->request((*section)->offset(),
                                        (*section)->size());
          llvm::ELF::Elf32_Word* value =
                      reinterpret_cast<llvm::ELF::Elf32_Word*>(region->start());

          size_t size = region->size() / sizeof(llvm::ELF::Elf32_Word);
          if (llvm::ELF::GRP_COMDAT == *value) {
            for (size_t index = 1; index < size; ++index)
              pInput.context()->getSectionTable()[value[index]] = NULL;
          }
          pInput.memArea()->release(region);
        }
        break;
      }
      /** relocation sections **/
      case LDFileFormat::Relocation: {
        assert(NULL != (*section)->getLink());
        size_t link_index = (*section)->getLink()->index();
        if (NULL == pInput.context()->getSectionTable()[link_index]) {
          // Relocation sections of group members should also be part of the
          // group. Thus, if the associated member sections are ignored, the
          // related relocations should be also ignored.
          *section = NULL;
        }
        break;
      }
      /** normal sections **/
      // FIXME: support Debug, Exception and Version Kinds
      case LDFileFormat::Debug:
      case LDFileFormat::Exception:
      case LDFileFormat::Version:
      /** Fall through **/
      case LDFileFormat::Regular:
      case LDFileFormat::Note:
      case LDFileFormat::MetaData: {
        if (!m_pELFReader->readRegularSection(pInput, m_Linker, **section))
          llvm::report_fatal_error(
                                llvm::Twine("can not read regular section `") +
                                (*section)->name() +
                                llvm::Twine("'.\n"));
        break;
      }
      /** target dependent sections **/
      case LDFileFormat::Target: {
        if (!m_pELFReader->readTargetSection(pInput, m_Linker, **section))
          llvm::report_fatal_error(
                        llvm::Twine("can not read target dependentsection `") +
                        (*section)->name() +
                        llvm::Twine("'.\n"));
        break;
      }
      /** BSS sections **/
      case LDFileFormat::BSS: {
        LDSection& output_bss = m_Linker.getOrCreateOutputSectHdr(
                                               (*section)->name(),
                                               LDFileFormat::BSS,
                                               llvm::ELF::SHT_NOBITS,
                                               llvm::ELF::SHF_ALLOC | llvm::ELF::SHF_WRITE);

        llvm::MCSectionData& sect_data = m_Linker.getOrCreateSectData(**section);
                                         /*  value, valsize, size*/
        llvm::MCFillFragment* frag =
                    new llvm::MCFillFragment(0x0,   1,       (*section)->size());

        uint64_t size = m_Linker.getLayout().appendFragment(*frag,
                                                            sect_data,
                                                            (*section)->align());
        output_bss.setSize(output_bss.size() + size);
        break;
      }
      // ignore
      case LDFileFormat::Null:
      case LDFileFormat::NamePool:
        continue;

    }
  } // end of for all sections

  return true;
}

/// readSymbols - read symbols into MCLinker from the input relocatable object.
bool ELFObjectReader::readSymbols(Input& pInput)
{
  assert(pInput.hasMemArea());

  LDSection* symtab_shdr = pInput.context()->getSection(".symtab");
  LDSection* strtab_shdr = symtab_shdr->getLink();
  if (NULL == symtab_shdr || NULL == strtab_shdr)
    return false;

  MemoryRegion* symtab_region = pInput.memArea()->request(symtab_shdr->offset(),
                                                          symtab_shdr->size());
  MemoryRegion* strtab_region = pInput.memArea()->request(strtab_shdr->offset(),
                                                          strtab_shdr->size());
  char* strtab = reinterpret_cast<char*>(strtab_region->start());
  bool result = m_pELFReader->readSymbols(pInput,
                                          m_Linker,
                                          *symtab_region,
                                          strtab);
  pInput.memArea()->release(symtab_region);
  pInput.memArea()->release(strtab_region);
  return result;
}

bool ELFObjectReader::readRelocations(Input& pInput)
{
  assert(pInput.hasMemArea());

  MemoryArea* mem = pInput.memArea();
  LDContext::sect_iterator section, sectEnd = pInput.context()->sectEnd();
  for (section = pInput.context()->sectBegin(); section != sectEnd; ++section) {
    // ignore the section if the LDSection* in input context is NULL
    if (NULL == *section)
        continue;

    if ((*section)->type() == llvm::ELF::SHT_RELA &&
        (*section)->kind() == LDFileFormat::Relocation) {
      MemoryRegion* region = mem->request((*section)->offset(), (*section)->size());
      bool result = m_pELFReader->readRela(pInput, m_Linker, **section, *region);
      mem->release(region);
      if (!result)
        return false;
    }
    else if ((*section)->type() == llvm::ELF::SHT_REL &&
             (*section)->kind() == LDFileFormat::Relocation) {
      MemoryRegion* region = mem->request((*section)->offset(), (*section)->size());
      bool result = m_pELFReader->readRel(pInput, m_Linker, **section, *region);
      mem->release(region);
      if (!result)
        return false;
    }
  }
  return true;
}

