/**
 * Author:    chainsboyo
 * Created:   02/05/2025
 * 
 * This is a simple solver for the Fortune Solitaire from The Zachtronics Solitaire Collection.
 **/


//#define NDEBUG
//#define PRINT_DEBUG


#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include <chrono>


//Ugly
std::vector<std::string_view> split(std::string_view s,char separator){
    std::vector<std::string_view> v;
    if(s.empty()) return v;
    //Allocate some memory
    v.reserve(64);
    bool finished = false;
    while(!finished){
        auto pos = s.find_first_of(separator,0);
        if(pos==std::string_view::npos){
            v.push_back(s);
            finished=true;
        }else{
            v.push_back(s.substr(0,pos));
            s.remove_prefix(pos+1);
        }
    }
    return v;
}




/**
 * Card 0 is reserved for null, last cards are used for padding
 * [1,13]  gold cards
 * [14,26] sword cards
 * [27,39] cups cards
 * [40,52] bastos cards (Clubs)
 * [53,76] tarot cards
 * NULL,
 * AG,2G,3G,4G,5G,6G,7G,8G,9G,10G,JG,QG,KG
 * AS,2S,3S,4S,5S,6S,7S,8S,9S,10S,JS,QS,KS
 * AC,2C,3C,4C,5C,6C,7C,8C,9C,10C,JC,QC,KC
 * AB,2B,3B,4B,5B,6B,7B,8B,9B,10B,JB,QB,KB
 * XT,0T,1T,2T,3T,4T,5T,6T,7T,8T,9T,10T,11T,12T,13T,14T,15T,16T,17T,18T,19T,20T,21T,YT
 * PAD,PAD
 * Low tarot stack starts with XT, high tarot stack starts with YT
 * Other suit stacks start with Ace (AG, AS, AC, AB)
 * Piles are stacks of cards that are still in play
 * Stacks are the final sorted stacks of cards
 * A single card can be put on top of the stack, this block the collection of the GSCB suits. This is always pile #0
**/

using card_t = uint8_t;
constexpr size_t cards_in_deck = 79;
constexpr size_t n_piles = 12;
constexpr card_t card_null = 0;
constexpr size_t unused_cards = 3;
//Ugly again
const std::string deck_string = "NULL,AG,2G,3G,4G,5G,6G,7G,8G,9G,10G,JG,QG,KG,AS,2S,3S,4S,5S,6S,7S,8S,9S,10S,JS,QS,KS,AC,2C,3C,4C,5C,6C,7C,8C,9C,10C,JC,QC,KC,AB,2B,3B,4B,5B,6B,7B,8B,9B,10B,JB,QB,KB,XT,0T,1T,2T,3T,4T,5T,6T,7T,8T,9T,10T,11T,12T,13T,14T,15T,16T,17T,18T,19T,20T,21T,YT,PAD,PAD";
static std::vector<std::string_view> card_name_array = split(deck_string,',');


inline bool is_gold (card_t card){return card>0  && card<14;};
inline bool is_sword(card_t card){return card>13 && card<27;};
inline bool is_cups (card_t card){return card>26 && card<40;};
inline bool is_basto(card_t card){return card>39 && card<53;};
inline bool is_tarot(card_t card){return card>52 && card<77;};

struct Board{
    //Each card holds the card that it is on top of
    card_t cards[cards_in_deck];
    //This help differentiaty between a single card being in a pile and a card being on top of the stacks
    card_t card_on_stacks;
};
struct State{
    Board board;
    //Point to the card that is at the top of each pile
    //Pile 0 is on the tapped card
    card_t top_cards[n_piles];
    card_t suit_stack[6];
    uint32_t moves;
    uint32_t stacked_cards;
    uint32_t past_action;
};

enum class ActionType:uint32_t{
    Null, MoveToPile, MoveToEmpty, MoveToStack
};
struct Action{
    //Before commiting an action source and dest refer to pile index
    //After commiting the action source and dest refer to card ids
    uint32_t source;
    uint32_t dest; 
    ActionType type;
};
struct PastAction{
    Action action;
    size_t past_index;
};

struct SearchResult{
    std::string replay;
    uint32_t n_moves;
    uint32_t expanded_nodes;
    uint64_t us_taken;
    bool finished;
};

std::string_view card_to_string(card_t card){
    assert(card<card_name_array.size());
    return card_name_array[card];
}
card_t string_to_card(std::string_view s){
    assert(card_name_array.size()==cards_in_deck);

    if(s.empty()) return card_null;
    
    for(size_t i = 0; i<card_name_array.size(); i++){
        if(card_name_array[i]==s) return (card_t)i;
    }

    std::cout<<"Cant convert card: "<<s<<'\n';
    return card_null;
}

void print_action(Action& action, State& state){
    std::cout << "Move card: ";
    auto card = state.top_cards[action.source];
    // std::cout << '('<<(uint32_t)card<<')';
    std::cout << card_to_string(card);
    std::cout << " to ";
    if(action.type==ActionType::MoveToStack){
        std::cout << "the top of stack\n";
    }else{
        auto dest = state.top_cards[action.dest];
        if(dest==card_null){
            std::cout << "empty pile\n";
        }else{
            std::cout << card_to_string(dest);
            std::cout << '\n';
        }
    }
}
void print_state(const State& state){
    std::cout<<"Moves: "<<state.moves<<"\n";
    std::cout<<"Cards left: "<<cards_in_deck-state.stacked_cards<<"\n";
    std::cout<<"Stack tops:\n";
    for(const auto& a:state.suit_stack){
        std::cout<<"\t"<<card_to_string(a)<<'\n';
    }
    std::cout<<"Piles:\n";
    for(auto card:state.top_cards){
        std::cout<<'\t';
        while(card){
            auto name = card_to_string(card);
            if(name.size()<3){
                std::cout<<' ';
            }
            std::cout<<name;
            card = state.board.cards[card];
            if(card) std::cout<<" -> ";
        }
        std::cout<<'\n';
    }
    std::cout<<'\n';
}

void print_board(const Board& board){
    for(size_t i = 0; i<cards_in_deck; i++){
        std::cout<<(int)board.cards[i];
    }
    std::cout<<board.card_on_stacks<<'\n';
}

bool is_game_finished(const State& state){
    //Game is finished if all the cards have been put in their stacks
    return state.stacked_cards == cards_in_deck;
}

bool stack_is_blocked(State& state){
    return state.top_cards[0]!=card_null;
}

bool can_stack(card_t card, card_t destination){
    //Can always move to an empty slot
    if(destination==card_null) return true;

    //If card is the same suit check if the difference between them is only 1 value
    if(is_gold(card))
    {
        if(is_gold(destination))  return (std::abs(card-destination)==1);
    }
    else if(is_sword(card))
    {
        if(is_sword(destination)) return (std::abs(card-destination)==1);
    }
    else if(is_cups(card))
    {
        if(is_cups(destination))  return (std::abs(card-destination)==1);
    }
    else if(is_basto(card))
    {
        if(is_basto(destination)) return (std::abs(card-destination)==1);
    }
    else if(is_tarot(card))
    {
        if(is_tarot(destination)) return (std::abs(card-destination)==1);
    }
    return false;
}

void generate_actions(State& state, std::vector<Action>& actions){
    //At some point the search starts wasting a lot of moves moving stuff into empty piles for no reason
    //other than just exploring.
    //Maybe prioritize moves that accomplish something?? Kinda stuck with using depth first search

    actions.clear();

    for(size_t i = 0; i<n_piles; i++){
        card_t card = state.top_cards[i];
        //Nothing to move
        if(card==card_null) continue;

        //Dont generate multiple move to empty as they are equal
        bool moved_to_empty = false;
        

        for(size_t j = 0; j<n_piles; j++){
            
            //Cant move to same pile
            if(j==i) continue;

            //Card at the top of the pile
            card_t top_card = state.top_cards[j];

            if(top_card==card_null && j ==0){
                //Move card to the stack
                actions.push_back({(uint32_t)i,(uint32_t)j,ActionType::MoveToStack});
            }else if(top_card==card_null && !moved_to_empty){
                //Prevent generating the same action twice
                moved_to_empty = true;
                //Move the card to an empty pile
                actions.push_back({(uint32_t)i,(uint32_t)j,ActionType::MoveToEmpty});
            }else if(can_stack(card,top_card) && j!=0){
                //Move the card to a non empty pile
                actions.push_back({(uint32_t)i,(uint32_t)j,ActionType::MoveToPile});
            }
        }
    }
}

State commit_action(const State& old_state,Action& action){
    //Assume action can always be performed aka dont check for bad state action pair
    //TODO: Maybe put some asserts down for that

    State state = old_state;
    state.moves += 1;
    if(action.type==ActionType::Null){
        //Ignore this as a move
        state.moves -=1;
    }else if(action.type==ActionType::MoveToEmpty || action.type==ActionType::MoveToPile){

        auto source_pile = action.source;
        auto dest_pile = action.dest;

        card_t card_destination = state.top_cards[dest_pile];
        //Card that is moving
        card_t moving_card = state.top_cards[source_pile];

        //This is only for obtaining the replay without having to use more space
        action.source = moving_card;
        action.dest = card_destination;

        //Action was generated wrong
        assert(moving_card!=card_null);
        assert(can_stack(moving_card,card_destination));

        //As long as they can be stacked keep moving them, also stop when there is no card to move
        while(can_stack(moving_card,card_destination) && moving_card!=card_null){
            //Card that was below the moving card
            auto card_below = state.board.cards[moving_card];
            
            //Make the card below the new top of the origin pile
            state.top_cards[source_pile] = card_below;
            //Make the moved card the new top of the destination pile
            state.top_cards[dest_pile] = moving_card;

            //Put the moving card on top of the moving pile(In this case a empty pile)
            state.board.cards[moving_card] = card_destination; 

            card_destination = moving_card;
            moving_card = card_below;
        }

    }else if(action.type==ActionType::MoveToStack){
        //Here there is no need to keep stacking as the limit of the pile is one card
        
        auto source_pile = action.source;

        card_t moving_card = state.top_cards[source_pile];

        action.source = moving_card;

        //Action was generated wrong
        assert(moving_card!=card_null);
        assert(state.top_cards[0]==card_null);
        
        
        //Set the new top of the stack
        state.top_cards[0] = moving_card;
        //Set the new top of the origin pile
        state.top_cards[source_pile] = state.board.cards[moving_card];

        //Now this card doesnt have anything below
        state.board.cards[moving_card] = card_null;
    }
    
    state.board.card_on_stacks = state.top_cards[0];

    //After moving lets check if we can move stuff into the stacks

    size_t available_stacks = 6;
    //If the stack is blocked only tarot cards can be put in their stack
    if(stack_is_blocked(state)) available_stacks=2;

    //If any card is put in the stack we need to check again
    //Only 72 stacking moves can done in a game so its not so bad??
    //TODO: This can be probably more efficient
    bool repeat = true;
    while(repeat){
        repeat = false;
        for(size_t i = 0 ; i<available_stacks; i++){
            for(size_t j = 1; j<n_piles; j++){
                if(can_stack(state.top_cards[j],state.suit_stack[i])){
                    //Need to recheck
                    repeat = true;
                    
                    //Update the stack top card
                    state.suit_stack[i] = state.top_cards[j];

                    //Update the pile top card
                    state.top_cards[j] = state.board.cards[state.top_cards[j]];

                    state.stacked_cards += 1;

                    //Recheck the same pile as is usual to put into the stack cards that are ordered in the same pile
                    j--;
                }
            }
        }
    }


    return state;
}

bool same_board(const Board& lhs,const Board& rhs){
    return (std::memcmp(&lhs,&rhs,sizeof(Board)) == 0); 
}

bool already_explored(const State& state, std::vector<Board>& closed_list){
    //@TODO: Maybe assume only the last n states can repeat given the nature of the game is hard to go back to old states
    if(closed_list.empty()) return false;
    //Iterate backwards given how the chance of the later moves being the same
    size_t last = closed_list.size()-1;
    //This is a really fucking dumbass dub
    // for(size_t i = closed_list.size()-1; i>=0; --i){
    for(size_t i = 0; i<closed_list.size(); i++){
        const Board& explored_state = closed_list[last-i];
        if(same_board(explored_state,state.board)){
            #ifdef PRINT_DEBUG
                print_board(state.board);
                print_board(explored_state);
            #endif
            return true;
        }
    }
    return false;
}

void generate_replay(std::vector<PastAction>& replay, size_t last_action, std::string& replay_string){
    //TODO: Reverse the replay order somehow so the replay string is read from top to bottom

    PastAction action = replay[last_action];
    while(action.action.type!=ActionType::Null){
        replay_string += card_to_string(action.action.source);
        replay_string += " to ";
        if(action.action.type==ActionType::MoveToStack) replay_string += "the stack ";
        else if(action.action.type==ActionType::MoveToEmpty) replay_string += "empty pile ";
        else replay_string += card_to_string(action.action.dest);
        replay_string += '\n';
        action = replay[action.past_index];
    }
}

SearchResult search_game(State& initial_state){
    auto time_start = std::chrono::steady_clock::now();
    SearchResult search_result;
    search_result.finished = false;

    std::vector<State> open_list;
    std::vector<Board> closed_list;
    std::vector<Action> actions;
    std::vector<PastAction> past_actions;

    //Arbitrary chunk of memory
    size_t reserved_memory = 64*1024;
    open_list.reserve(reserved_memory);
    closed_list.reserve(reserved_memory);
    past_actions.reserve(reserved_memory);

    //32 should me more than enough actions in a move
    actions.reserve(32);

    past_actions.push_back({{0,0,ActionType::Null},0});
    initial_state.past_action = 0;

    open_list.push_back(initial_state);

    size_t expanded_nodes = 0;
    //Any game should be searchable under this limit
    size_t nodes_limit = 10*1000;
    while(!open_list.empty() && expanded_nodes<nodes_limit){
        //82 bytes is not that expensive so make a copy
        State state = open_list.back();
        //We can immediatly free now
        open_list.pop_back();
        #ifdef PRINT_DEBUG
            std::cout<<"Node#"<<expanded_nodes<<'\n';
        #endif
        expanded_nodes += 1;

        generate_actions(state, actions);
        #ifdef PRINT_DEBUG
            std::cout << "Action generation correct. Generated: " << actions.size() << " actions\n";
        #endif
        for(auto& action:actions){
            #ifdef PRINT_DEBUG
                print_action(action,state);
                print_state(state);
            #endif

            State new_state = commit_action(state,action);

            #ifdef PRINT_DEBUG
                std::cout << "Action commit correct\n";
                print_state(new_state);
            #endif

            if(is_game_finished(new_state)){
                past_actions.push_back(PastAction{action,new_state.past_action});

                //Fill the result
                generate_replay(past_actions,past_actions.size()-1,search_result.replay);
                search_result.expanded_nodes = expanded_nodes;
                search_result.finished = true;
                search_result.n_moves = new_state.moves;

                auto time_end = std::chrono::steady_clock::now();
                search_result.us_taken = std::chrono::duration_cast<std::chrono::microseconds>(time_end-time_start).count();

                return search_result;
            }

            if(!already_explored(new_state,closed_list)){
                
                past_actions.push_back(PastAction{action,new_state.past_action});
                new_state.past_action=past_actions.size()-1;

                closed_list.push_back(new_state.board);
                open_list.push_back(new_state);
            }
        }
    }
    if(expanded_nodes>=nodes_limit){
        std::cout<<"Node limit reached\n";
    }
    search_result.expanded_nodes = expanded_nodes;
    auto time_end = std::chrono::steady_clock::now();
    search_result.us_taken = std::chrono::duration_cast<std::chrono::microseconds>(time_end-time_start).count();
    return search_result;
}

bool string_to_state(std::string_view s, State& state){

    //Set everything to 0
    state = State{};
    bool seen[cards_in_deck] = {};

    std::vector<std::string_view> piles = split(s,'|');
    //There must be 18 different piles
    
    if(piles.size()!=18){
        std::cout<<"There must be 18 piles in the input string. Found: "<<piles.size()<<'\n';
        return false;
    }
    for(size_t i = 0; i<12; i++){
        auto& pile = piles[i];
        auto cards_in_pile = split(pile,',');
        if(i==0){
            if(cards_in_pile.size()>2){
                std::cout<<"Too many cards on pile on top of stack\n";
                return false;
            }
        }
        card_t last_card = card_null;
        for(const auto& card_name:cards_in_pile){
            card_t card = string_to_card(card_name);
            assert(card<cards_in_deck);
            if(seen[card]){
                std::cout<<"Repeated card found: "<<card_name<<'\n';
                return false;
            }
            seen[card]=true;
            state.board.cards[card] = last_card;
            last_card = card;
        }
        //Set top of pile
        state.top_cards[i] = last_card;
    }
    state.stacked_cards = (uint32_t)unused_cards;
    //Stack piles
    for(size_t i = 12; i<18; i++){
        auto& pile = piles[i];
        auto cards_in_pile = split(pile,',');
        card_t last_card = card_null;
        for(const auto& card_name:cards_in_pile){
            card_t card = string_to_card(card_name);
            assert(card<cards_in_deck);
            if(seen[card]){
                std::cout<<"Repeated card found: "<<card_name<<'\n';
                return false;
            }
            seen[card]=true;
            state.board.cards[card] = last_card;
            if(!can_stack(card,last_card)){
                std::cout<<"Cant stack card: "<<card_name<<"\n";
                return false;
            }
            last_card = card;
            state.stacked_cards+=1;
        }
        state.suit_stack[i-12] = last_card;
    }


    return true;

}

int main(){
    assert(card_name_array.size()==cards_in_deck);
    /**
     * State string represents each one of the piles, each card separated by comes and from furthest below card to top card
     * Each pile is separated by a vertical bar
     * Empty piles are simply empty
     * Start with the stack card
     * Followed by the playables piles
     * Next is the low tarot stack, high tarot stack, gold, swords, cups, bastos (clubs)
     * There must be a total of 18 piles
    **/

    std::string state_string = "|KC,JG,10G,3B,QG,4C,KB|8S,9S,QB,16T,7G,10T,18T|6B,13T,QC,8G,7B,21T,9B|3S,12T,5B,7T,2G,KS,8B|JC,2T,3G,5C,7S,4G,JB||0T,6G,10B,17T,2C,8T,5T|6S,15T,6C,20T,JS,QS,4B|7C,9C,10C,4S,10S,2B,9T|11T,9G,6T,3T,KG,14T,3C|4T,8C,2S,5S,1T,19T,5G|XT|YT|AG|AS|AC|AB";
    State initial_state;
    bool success = string_to_state(state_string,initial_state);
    if(!success){
        std::cout<<"Failed to read the state\n";
        return EXIT_FAILURE;
    }

    SearchResult result = search_game(initial_state);

    std::cout<<"Time taken: "<<result.us_taken<<"us\n";
    std::cout<<"Nodes expanded: "<<result.expanded_nodes<<'\n';
    std::cout<<"Is beatable: "<<result.finished<<'\n';
    // std::cout<<"Replay:\n"<<result.replay<<'\n';
    return EXIT_SUCCESS;
}