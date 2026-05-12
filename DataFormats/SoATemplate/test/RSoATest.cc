#include <cstdlib>
#include <iostream>
#include <memory>

#include "DataFormats/SoATemplate/test/RSoATest.h"

#include <ROOT/REntry.hxx>
#include <ROOT/RNTupleModel.hxx>
#include <ROOT/RNTupleReader.hxx>
#include <ROOT/RNTupleWriter.hxx>


using RSoA = RSoALayout<>;

int main() {
  const std::size_t slSize = 10;
  const std::size_t slBufferSize = RSoA::computeDataSize(slSize);
  std::unique_ptr<std::byte, decltype(std::free)*> slBuffer{
      reinterpret_cast<std::byte*>(aligned_alloc(RSoA::alignment, slBufferSize)), std::free};

  RSoA sl{slBuffer.get(), slSize};
  RSoA::View slv{sl};

  for (RSoA::View::size_type i = 0; i < slv.metadata().size(); ++i) {
    auto slvi = slv[i];
    slvi.x() = static_cast<float>(i);
    slvi.y() = static_cast<float>(i * 2);
  }

  std::cout << "RSoA contents:" << std::endl;
  for (RSoA::View::size_type i = 0; i < slv.metadata().size(); ++i) {
    auto slvi = slv[i];
    std::cout << "Row " << i << ": x = " << slvi.x() << ", y = " << slvi.y() << std::endl;
  }

  std::cout << "Start RNTuple Writing " << std::endl;

  auto c = TClass::GetClass("RSoALayout<128, false>");

  if(!c){
    std::cout << "C nullptr" << std::endl;
  }

  auto model = ROOT::RNTupleModel::CreateBare();
  model->AddField(ROOT::RFieldBase::Create("soaTest", "RSoALayout<128, false>").Unwrap());

  constexpr const char* kFileName = "testRNTuple.root";
  constexpr const char* kNTupleName = "ntpl";

  auto writer = ROOT::RNTupleWriter::Recreate(std::move(model), kNTupleName, kFileName);
  auto entry = writer->GetModel().CreateBareEntry();

  entry->BindRawPtr("soaTest", &sl);
  writer->Fill(*entry);

  return EXIT_SUCCESS;
}
