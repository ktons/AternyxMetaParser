#include <string>
#include <vector>

#include "../meta/meta_attributes.h"

namespace UserStruct {
CLASS(ClassA, Serialization) {
 public:
  int k;
  std::string name;
  std::vector<float> lengthList_;
  META(Runtime) int scratch;
};

STRUCT(DataBlock, Serialization, EditorUI, Summary) {
  int a;
  int b;
  std::string name;
};
}  // namespace UserStruct