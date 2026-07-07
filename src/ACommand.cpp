#include "ACommand.hpp"

// 基底ポインタ経由でdeleteされるため、純粋仮想クラスでも実体が必要
ACommand::~ACommand() {
}
