#include "resource_exchange.hpp"
#include <boost/container/flat_map.hpp>
#include <cmath>
#include <eosiolib/currency.hpp>
#include <eosiolib/eosio.hpp>
#include <eosiolib/print.hpp>
#include <eosiolib/types.hpp>

namespace eosio {
void resource_exchange::apply(account_name contract, account_name act) {
  // print("Running: ", name{contract}, " - ", name{act}, "\n");
  switch (act) {
    case N(transfer): {
      auto tx = unpack_action_data<currency::transfer>();
      if (tx.from != _contract) {
        deposit(tx);
      }
      break;
    }
    case N(withdraw): {
      auto tx = unpack_action_data<withdraw_tx>();
      withdraw(tx.to, tx.quantity);
      break;
    }
  }
}

void resource_exchange::deposit(currency::transfer tx) {
  require_auth(tx.from);
  eosio_assert(tx.quantity.is_valid(), "invalid quantity");
  eosio_assert(tx.quantity.symbol == asset().symbol,
               "asset must be system token");

  auto itr = accounts.find(tx.from);
  if (itr == accounts.end()) {
    itr = accounts.emplace(_self, [&](auto& acnt) { acnt.owner = tx.from; });
  }

  accounts.modify(itr, 0, [&](auto& acnt) { acnt.balance += tx.quantity; });
}
void resource_exchange::withdraw(account_name to, asset quantity) {
  // cleos set account permission {contract_name} active '{"threshold":
  // 1,"keys": [{"key": "{key_for_permision}","weight": 1}],"accounts":
  // [{"permission":{"actor":"{contract_name}","permission":"eosio.code"},"weight":1}]}'
  // owner
  require_auth(to);
  eosio_assert(quantity.is_valid(), "invalid quantity");
  eosio_assert(quantity.amount > 0, "must withdraw positive quantity");

  auto itr = accounts.find(to);
  eosio_assert(itr != accounts.end(), "unknown account");

  accounts.modify(itr, 0, [&](auto& acnt) {
    eosio_assert(acnt.balance >= quantity, "insufficient balance");
    acnt.balance -= quantity;
  });

  // TODO first unstake some tokens, 3days later send
  action(permission_level(_contract, N(active)), N(eosio.token), N(transfer),
         std::make_tuple(_contract, to, quantity, std::string("")))
      .send();

  if (itr->is_empty()) {
    accounts.erase(itr);
  }
}

void resource_exchange::delegatebw(account_name receiver,
                                   asset stake_net_quantity,
                                   asset stake_cpu_quantity) {
  action(permission_level(_contract, N(active)), N(eosio.system), N(delegatebw),
         std::make_tuple(_contract, receiver, stake_net_quantity,
                         stake_cpu_quantity))
      .send();
}

void resource_exchange::undelegatebw(account_name receiver,
                                     asset stake_net_quantity,
                                     asset stake_cpu_quantity) {
  action(permission_level(_contract, N(active)), N(eosio.system),
         N(undelegatebw),
         std::make_tuple(_contract, receiver, stake_net_quantity,
                         stake_cpu_quantity))
      .send();
}

void resource_exchange::buystake(account_name from, account_name to, asset net,
                                 asset cpu) {
  // TODO
  // enouth funds liguid to stake
  //
  require_auth(from);
  eosio_assert(net.is_valid() && cpu.is_valid(), "invalid quantity");

  auto state = contract_state.get();
  eosio_assert(state.liquid_funds >= (net + cpu), "not enough resources in exchange");
  eosio_assert(net.symbol == asset().symbol && cpu.symbol == asset().symbol,
               "asset must be system token");

  auto itr = accounts.find(from);
  eosio_assert(itr != accounts.end(), "account not found");
  // calculate cost based on supply
  int64_t cost = calculate_cost(net, cpu);
  eosio_assert(itr->balance.amount >= cost, "not enough funds on account");

  // TODO do this on the next cycle
  // deduce account
  accounts.modify(itr, 0, [&](auto& acnt) { acnt.balance.amount -= cost; });
  // delegate resources
  delegatebw(to, net, cpu);
}

void resource_exchange::sellstake(account_name from, account_name to, asset net,
                                  asset cpu) {
  // TODO
}

int64_t resource_exchange::calculate_cost(asset stake_net_quantity,
                                          asset stake_cpu_quantity) {
  eosio_assert(contract_state.exists(), "No contract state available");
  auto state = contract_state.get();

  // TO do, trottle curve
  int64_t cost_per_token =
      -1 / (state.liquid_funds.amount -
            state.get_total().amount * 2);  // TODO find optimal function
  return cost_per_token * (stake_net_quantity.amount + stake_cpu_quantity.amount);
}
}  // namespace eosio

extern "C" {
[[noreturn]] void apply(uint64_t receiver, uint64_t code, uint64_t action) {
  eosio::resource_exchange ex(receiver);
  ex.apply(code, action);
  eosio_exit(0);
}
}