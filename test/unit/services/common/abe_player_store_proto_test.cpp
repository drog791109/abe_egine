#include "player_store.pb.h"

#include "../../abe_test.h"

#include <string>

namespace store = abe::proto::store;

static void fill_meta(store::PB_STORE_META* meta)
{
    meta->set_schema_version(1u);
    meta->set_data_version(2u);
    meta->set_created_at_ms(1000u);
    meta->set_updated_at_ms(2000u);
}

static int test_player_round_trip(void)
{
    store::PB_PLAYER_DATA player;
    store::PB_PLAYER_DATA copy;
    std::string blob;

    fill_meta(player.mutable_meta());
    player.set_uid(10001u);
    player.set_account_id(90001u);
    player.set_state(store::PLAYER_STATE_ONLINE);
    player.mutable_profile()->set_nickname("abe");
    player.mutable_profile()->set_level(12u);
    player.mutable_wallet()->set_gold(3000);
    player.mutable_wallet()->set_diamond(20);
    player.set_room_id(70001u);

    TEST_REQUIRE(player.SerializeToString(&blob));
    TEST_REQUIRE(copy.ParseFromString(blob));
    TEST_REQUIRE(copy.uid() == 10001u);
    TEST_REQUIRE(copy.account_id() == 90001u);
    TEST_REQUIRE(copy.state() == store::PLAYER_STATE_ONLINE);
    TEST_REQUIRE(copy.profile().nickname() == "abe");
    TEST_REQUIRE(copy.profile().level() == 12u);
    TEST_REQUIRE(copy.wallet().gold() == 3000);
    TEST_REQUIRE(copy.room_id() == 70001u);
    return ABE_TEST_STATUS_OK;
}

static int test_bag_groups_items(void)
{
    store::PB_BAG_DATA bag;

    fill_meta(bag.mutable_meta());
    bag.set_uid(10001u);
    bag.set_capacity(200u);
    bag.add_item_list()->set_item_id(1u);
    bag.add_equipment_list()->set_equip_id(2001u);
    bag.add_appearance_list()->set_appearance_id(3001u);

    TEST_REQUIRE(bag.item_list_size() == 1);
    TEST_REQUIRE(bag.equipment_list_size() == 1);
    TEST_REQUIRE(bag.appearance_list_size() == 1);
    TEST_REQUIRE(bag.capacity() == 200u);
    return ABE_TEST_STATUS_OK;
}

static int test_task_and_mail_state(void)
{
    store::PB_TASK_DATA tasks;
    store::PB_MAIL_DATA mail;
    store::PB_MAIL_LIST mail_list;

    fill_meta(tasks.mutable_meta());
    tasks.set_uid(10001u);
    tasks.add_task_list()->set_state(store::TASK_STATE_ACTIVE);
    tasks.add_task_list()->set_state(store::TASK_STATE_CLAIMABLE);

    fill_meta(mail.mutable_meta());
    mail.set_mail_id(80001u);
    mail.set_uid(10001u);
    mail.set_state(store::MAIL_STATE_UNREAD);
    mail.set_title("hello");
    mail.add_attachment_list()->set_item_id(1u);
    *mail_list.add_mail_list() = mail;

    TEST_REQUIRE(tasks.task_list_size() == 2);
    TEST_REQUIRE(tasks.task_list(0).state() == store::TASK_STATE_ACTIVE);
    TEST_REQUIRE(tasks.task_list(1).state() == store::TASK_STATE_CLAIMABLE);
    TEST_REQUIRE(mail.state() == store::MAIL_STATE_UNREAD);
    TEST_REQUIRE(mail.attachment_list_size() == 1);
    TEST_REQUIRE(mail_list.mail_list_size() == 1);
    TEST_REQUIRE(mail_list.mail_list(0).mail_id() == 80001u);
    return ABE_TEST_STATUS_OK;
}

int main()
{
    if (test_player_round_trip() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_bag_groups_items() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    if (test_task_and_mail_state() != ABE_TEST_STATUS_OK) {
        return ABE_TEST_STATUS_FAILED;
    }
    return ABE_TEST_STATUS_OK;
}
