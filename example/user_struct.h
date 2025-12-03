#include <string>
#include <vector>

#include "../meta/meta_attributes.h"

namespace UserStruct {
CLASS(ClassA, Serialization) {
 public:
  int k;
  std::string name;
  std::vector<float> lengthList_;
};

STRUCT(DataBlock, EditorUI) {
  int a;
  int b;
  std::string name;
};
}  // namespace UserStruct