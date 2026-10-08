#import "SolitaireViewController.h"

#import "native_session.h"
#import "board_layout.h"
#import "position.h"

#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>
#include <string.h>

namespace {
constexpr uint32_t kSaveMagic = 0x4f4b5347; // OKSG
constexpr uint32_t kSaveVersion = 1;
NSString *const kSavedGameKey = @"still-solvable.saved-game.v1";
NSString *const kCardsDirectory = @"Cards";
}

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t gameBytes;
    Game game;
} OKSavedGameBlob;

@class OKBoardView;

@protocol OKBoardViewDelegate <NSObject>
- (void)boardViewDidTapStock:(OKBoardView *)board;
- (void)boardView:(OKBoardView *)board didTapCardKind:(PileKind)kind
            index:(NSInteger)index card:(NSInteger)card;
- (void)boardView:(OKBoardView *)board didLongPressCardKind:(PileKind)kind
            index:(NSInteger)index card:(NSInteger)card;
- (void)boardView:(OKBoardView *)board didTapDestinationKind:(PileKind)kind
            index:(NSInteger)index;
@end

@interface OKCardButton : UIButton
@property (nonatomic) PileKind pileKind;
@property (nonatomic) NSInteger pileIndex;
@property (nonatomic) NSInteger cardIndex;
@property (nonatomic) BOOL faceUp;
@property (nonatomic) NSInteger identityKey;
@end

@implementation OKCardButton
@end

@interface OKPileButton : UIButton
@property (nonatomic) PileKind pileKind;
@property (nonatomic) NSInteger pileIndex;
@property (nonatomic) BOOL isStock;
@end

@implementation OKPileButton
@end

@interface OKBoardView : UIView
@property (nonatomic, weak) id<OKBoardViewDelegate> delegate;
- (void)renderGame:(const Game *)game animated:(BOOL)animated;
- (void)selectKind:(PileKind)kind index:(NSInteger)index card:(NSInteger)card;
- (void)clearSelection;
- (void)setLegalDestinations:(NSArray<NSString *> *)destinations;
- (void)setHintSourceKind:(PileKind)sourceKind sourceIndex:(NSInteger)sourceIndex
               sourceCard:(NSInteger)sourceCard destinationKind:(PileKind)destinationKind
        destinationIndex:(NSInteger)destinationIndex;
- (void)clearHint;
@end

@implementation OKBoardView {
    Game _game;
    BOOL _hasGame;
    NSMutableDictionary<NSNumber *, OKCardButton *> *_cardButtons;
    NSMutableDictionary<NSString *, OKPileButton *> *_slotButtons;
    NSMutableDictionary<NSNumber *, UIImage *> *_imageCache;
    NSMutableSet<NSString *> *_legalDestinations;
    PileKind _selectedKind;
    NSInteger _selectedIndex;
    NSInteger _selectedCard;
    BOOL _hasSelection;
    BOOL _hasHint;
    PileKind _hintSourceKind;
    NSInteger _hintSourceIndex;
    NSInteger _hintSourceCard;
    PileKind _hintDestinationKind;
    NSInteger _hintDestinationIndex;
    CGFloat _cardWidth;
    CGFloat _cardHeight;
    CGFloat _topCardWidth, _topCardHeight, _topInset;
    CGFloat _columnGap;
    CGFloat _columnX[7];
    CGFloat _tableauY;
    CGFloat _faceDownSpacing;
    CGFloat _faceUpSpacing;
    UISelectionFeedbackGenerator *_selectionFeedback;
}

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.backgroundColor = UIColor.clearColor;
        self.clipsToBounds = NO;
        _cardButtons = [NSMutableDictionary dictionaryWithCapacity:52];
        _slotButtons = [NSMutableDictionary dictionaryWithCapacity:13];
        _imageCache = [NSMutableDictionary dictionaryWithCapacity:53];
        _legalDestinations = [NSMutableSet set];
        _selectionFeedback = [[UISelectionFeedbackGenerator alloc] init];
        [self buildSlots];
    }
    return self;
}

- (NSString *)slotKeyForKind:(PileKind)kind index:(NSInteger)index {
    return [NSString stringWithFormat:@"%d:%ld", (int)kind, (long)index];
}

- (void)buildSlots {
    // Keep the familiar Klondike map: stock and waste at the left, an open
    // breathing space, four foundations on the right, then seven columns.
    for (NSInteger i = 0; i < 2; i++) {
        PileKind kind = i == 0 ? LOC_STOCK : LOC_WASTE;
        OKPileButton *button = [self newSlotWithKind:kind index:0 stock:(i == 0)];
        _slotButtons[[self slotKeyForKind:kind index:0]] = button;
        [self addSubview:button];
    }
    for (NSInteger i = 0; i < 4; i++) {
        OKPileButton *button = [self newSlotWithKind:LOC_FOUNDATION index:i stock:NO];
        _slotButtons[[self slotKeyForKind:LOC_FOUNDATION index:i]] = button;
        [self addSubview:button];
    }
    for (NSInteger i = 0; i < 7; i++) {
        OKPileButton *button = [self newSlotWithKind:LOC_TABLEAU index:i stock:NO];
        _slotButtons[[self slotKeyForKind:LOC_TABLEAU index:i]] = button;
        [self addSubview:button];
    }
}

- (OKPileButton *)newSlotWithKind:(PileKind)kind index:(NSInteger)index stock:(BOOL)stock {
    OKPileButton *button = [OKPileButton buttonWithType:UIButtonTypeCustom];
    button.pileKind = kind;
    button.pileIndex = index;
    button.isStock = stock;
    button.backgroundColor = [UIColor colorWithRed:0.025 green:0.23 blue:0.12 alpha:0.24];
    button.layer.cornerRadius = 7.0;
    button.layer.borderWidth = 1.0;
    button.layer.borderColor = [UIColor colorWithWhite:1.0 alpha:0.19].CGColor;
    button.clipsToBounds = NO;
    button.accessibilityTraits = UIAccessibilityTraitButton;
    if (stock) {
        button.accessibilityLabel = @"Draw from stock";
        button.accessibilityIdentifier = @"pile.stock";
        [button addTarget:self action:@selector(stockSlotPressed:) forControlEvents:UIControlEventTouchUpInside];
    } else {
        button.accessibilityLabel = [self labelForPile:kind index:index];
        button.accessibilityIdentifier = [NSString stringWithFormat:@"pile.%@.%ld",
                                          kind == LOC_WASTE ? @"waste" :
                                          (kind == LOC_FOUNDATION ? @"foundation" : @"tableau"),
                                          (long)index];
        [button addTarget:self action:@selector(slotPressed:) forControlEvents:UIControlEventTouchUpInside];
    }
    if (kind == LOC_FOUNDATION) {
        // Empty foundations accept any Ace; a fixed suit marker would imply
        // a restriction that the rules do not have.
        [button setTitle:@"A" forState:UIControlStateNormal];
        [button setTitleColor:[UIColor colorWithWhite:1 alpha:0.23] forState:UIControlStateNormal];
        button.titleLabel.font = [UIFont fontWithName:@"Georgia-Bold" size:14];
        button.titleLabel.textAlignment = NSTextAlignmentCenter;
        button.titleLabel.numberOfLines = 2;
    } else if (kind == LOC_TABLEAU) {
        [button setTitle:@"K" forState:UIControlStateNormal];
        [button setTitleColor:[UIColor colorWithWhite:1 alpha:0.20] forState:UIControlStateNormal];
        button.titleLabel.font = [UIFont fontWithName:@"Georgia-Bold" size:18];
    }
    return button;
}

- (NSString *)labelForPile:(PileKind)kind index:(NSInteger)index {
    if (kind == LOC_WASTE) return @"Waste pile";
    if (kind == LOC_FOUNDATION) return [NSString stringWithFormat:@"Foundation %ld", (long)(index + 1)];
    if (kind == LOC_TABLEAU) return [NSString stringWithFormat:@"Column %ld", (long)(index + 1)];
    return @"Stock pile";
}

- (void)layoutSubviews {
    [super layoutSubviews];
    [self layoutBoardAnimated:NO];
}

- (void)layoutBoardAnimated:(BOOL)animated {
    CGFloat width = CGRectGetWidth(self.bounds);
    CGFloat height = CGRectGetHeight(self.bounds);
    if (width < 1.0 || height < 1.0) return;

    int down[7] = {0}, up[7] = {0};
    if (_hasGame) {
        for (int column = 0; column < 7; column++) {
            const Pile *pile = &_game.tableau[column];
            int hidden = 0;
            while (hidden < pile->count && !pile->cards[hidden].face_up) hidden++;
            down[column] = hidden;
            up[column] = MAX(0, pile->count - hidden - 1);
        }
    }
    OKBoardLayout layout = ok_board_layout(width, height, width > height, down, up);
    _cardWidth = layout.card_width;
    _cardHeight = layout.card_height;
    _topCardWidth = layout.top_width;
    _topCardHeight = layout.top_height;
    _topInset = layout.top_inset;
    _columnGap = layout.column_gap;
    for (int c = 0; c < 7; c++) _columnX[c] = layout.column_x[c];
    _tableauY = layout.tableau_y;
    _faceDownSpacing = layout.down_spacing;
    _faceUpSpacing = layout.up_spacing;

    NSMutableArray<NSValue *> *slotFrames = [NSMutableArray arrayWithCapacity:13];
    [slotFrames addObject:[NSValue valueWithCGRect:CGRectMake(_columnX[0] + _topInset, 0, _topCardWidth, _topCardHeight)]];
    [slotFrames addObject:[NSValue valueWithCGRect:CGRectMake(_columnX[1] + _topInset, 0, _topCardWidth, _topCardHeight)]];
    for (NSInteger f = 0; f < 4; f++) {
        [slotFrames addObject:[NSValue valueWithCGRect:CGRectMake(_columnX[f + 3] + _topInset, 0, _topCardWidth, _topCardHeight)]];
    }
    for (NSInteger c = 0; c < 7; c++) {
        [slotFrames addObject:[NSValue valueWithCGRect:CGRectMake(_columnX[c], _tableauY, _cardWidth, _cardHeight)]];
    }

    void (^applyFrames)(void) = ^{
        NSArray<NSString *> *topKeys = @[
            [self slotKeyForKind:LOC_STOCK index:0], [self slotKeyForKind:LOC_WASTE index:0],
            [self slotKeyForKind:LOC_FOUNDATION index:0], [self slotKeyForKind:LOC_FOUNDATION index:1],
            [self slotKeyForKind:LOC_FOUNDATION index:2], [self slotKeyForKind:LOC_FOUNDATION index:3],
            [self slotKeyForKind:LOC_TABLEAU index:0], [self slotKeyForKind:LOC_TABLEAU index:1],
            [self slotKeyForKind:LOC_TABLEAU index:2], [self slotKeyForKind:LOC_TABLEAU index:3],
            [self slotKeyForKind:LOC_TABLEAU index:4], [self slotKeyForKind:LOC_TABLEAU index:5],
            [self slotKeyForKind:LOC_TABLEAU index:6]
        ];
        for (NSUInteger i = 0; i < topKeys.count; i++) {
            _slotButtons[topKeys[i]].frame = slotFrames[i].CGRectValue;
        }
        [self positionCards];
    };

    if (animated && !UIAccessibilityIsReduceMotionEnabled()) {
        [UIView animateWithDuration:0.28 delay:0 usingSpringWithDamping:0.94 initialSpringVelocity:0.0
                            options:UIViewAnimationOptionBeginFromCurrentState | UIViewAnimationOptionAllowUserInteraction
                         animations:applyFrames completion:nil];
    } else {
        applyFrames();
    }
}

- (void)renderGame:(const Game *)game animated:(BOOL)animated {
    if (!game) return;
    _game = *game;
    _hasGame = YES;
    [self layoutBoardAnimated:animated];
}

- (NSInteger)keyForCard:(Card)card {
    // This identity is private view bookkeeping only. Face-down views never
    // expose it in their image, accessibility text, labels, or logs.
    return (NSInteger)card.suit * 13 + (NSInteger)card.rank;
}

- (UIImage *)imageForCard:(Card)card {
    NSInteger key = card.face_up ? [self keyForCard:card] : 1000;
    UIImage *cached = _imageCache[@(key)];
    if (cached) return cached;

    NSString *file = @"back.png";
    if (card.face_up) {
        static NSArray<NSString *> *suitNames;
        static dispatch_once_t onceToken;
        dispatch_once(&onceToken, ^{ suitNames = @[@"c", @"d", @"h", @"s"]; });
        file = [NSString stringWithFormat:@"%@%02u.png", suitNames[card.suit % 4], card.rank];
    }
    NSString *path = [[[NSBundle mainBundle] resourcePath] stringByAppendingPathComponent:kCardsDirectory];
    UIImage *image = [UIImage imageWithContentsOfFile:[path stringByAppendingPathComponent:file]];
    if (!image) image = [self fallbackImageForCard:card];
    if (image) _imageCache[@(key)] = image;
    return image;
}

- (UIImage *)fallbackImageForCard:(Card)card {
    CGSize size = CGSizeMake(140, 196);
    UIGraphicsBeginImageContextWithOptions(size, NO, 1.0);
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    CGRect rect = CGRectMake(0, 0, size.width, size.height);
    UIBezierPath *outer = [UIBezierPath bezierPathWithRoundedRect:rect cornerRadius:11];
    if (card.face_up) {
        [[UIColor colorWithRed:0.99 green:0.985 blue:0.95 alpha:1.0] setFill];
        [outer fill];
        [[UIColor colorWithWhite:0.14 alpha:1.0] setStroke];
        outer.lineWidth = 2;
        [outer stroke];
        NSArray *symbols = @[@"♣", @"♦", @"♥", @"♠"];
        UIColor *ink = card_is_red(card) ? [UIColor colorWithRed:0.77 green:0.09 blue:0.12 alpha:1]
                                          : [UIColor colorWithWhite:0.08 alpha:1];
        NSString *rank = card.rank == 1 ? @"A" : (card.rank == 11 ? @"J" : (card.rank == 12 ? @"Q" : (card.rank == 13 ? @"K" : [NSString stringWithFormat:@"%u", card.rank])));
        NSDictionary *corner = @{NSFontAttributeName:[UIFont boldSystemFontOfSize:25], NSForegroundColorAttributeName:ink};
        [rank drawAtPoint:CGPointMake(12, 8) withAttributes:corner];
        [symbols[card.suit % 4] drawAtPoint:CGPointMake(12, 34) withAttributes:@{NSFontAttributeName:[UIFont systemFontOfSize:22], NSForegroundColorAttributeName:ink}];
        [symbols[card.suit % 4] drawInRect:CGRectMake(24, 56, 92, 92) withAttributes:@{NSFontAttributeName:[UIFont systemFontOfSize:76], NSForegroundColorAttributeName:ink}];
    } else {
        [[UIColor whiteColor] setFill]; [outer fill];
        CGRect inset = CGRectInset(rect, 6, 6);
        UIBezierPath *inner = [UIBezierPath bezierPathWithRoundedRect:inset cornerRadius:8];
        [[UIColor colorWithRed:0.08 green:0.25 blue:0.58 alpha:1] setFill]; [inner fill];
        [[UIColor colorWithRed:0.73 green:0.83 blue:0.97 alpha:1] setStroke];
        inner.lineWidth = 2; [inner stroke];
        CGContextSetStrokeColorWithColor(ctx, [UIColor colorWithWhite:1 alpha:0.16].CGColor);
        CGContextSetLineWidth(ctx, 1);
        for (CGFloat x = -size.height; x < size.width; x += 12) {
            CGContextMoveToPoint(ctx, x, 8); CGContextAddLineToPoint(ctx, x + size.height, size.height - 8);
        }
        CGContextStrokePath(ctx);
    }
    UIImage *image = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return image;
}

- (NSString *)rankName:(uint8_t)rank {
    switch (rank) {
        case 1: return @"Ace";
        case 11: return @"Jack";
        case 12: return @"Queen";
        case 13: return @"King";
        default: return [NSString stringWithFormat:@"%u", rank];
    }
}

- (NSString *)suitName:(uint8_t)suit {
    static NSArray<NSString *> *names;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{ names = @[@"clubs", @"diamonds", @"hearts", @"spades"]; });
    return names[suit % 4];
}

- (OKCardButton *)buttonForCard:(Card)card kind:(PileKind)kind index:(NSInteger)index
                      cardIndex:(NSInteger)cardIndex frame:(CGRect)frame {
    NSInteger identity = [self keyForCard:card];
    OKCardButton *button = _cardButtons[@(identity)];
    BOOL wasFaceUp = button.faceUp;
    if (!button) {
        button = [OKCardButton buttonWithType:UIButtonTypeCustom];
        button.identityKey = identity;
        button.layer.cornerRadius = 7.0;
        button.layer.borderWidth = 0.5;
        button.layer.borderColor = [UIColor colorWithWhite:0 alpha:0.32].CGColor;
        button.layer.shadowColor = UIColor.blackColor.CGColor;
        button.layer.shadowOpacity = 0.18;
        button.layer.shadowRadius = 3.0;
        button.layer.shadowOffset = CGSizeMake(0, 2);
        button.clipsToBounds = NO;
        [button addTarget:self action:@selector(cardPressed:) forControlEvents:UIControlEventTouchUpInside];
        UILongPressGestureRecognizer *longPress = [[UILongPressGestureRecognizer alloc]
            initWithTarget:self action:@selector(cardLongPressed:)];
        longPress.minimumPressDuration = 0.32;
        longPress.cancelsTouchesInView = YES;
        [button addGestureRecognizer:longPress];
        _cardButtons[@(identity)] = button;
        [self addSubview:button];
    }
    if (button.superview != self) [self addSubview:button];
    button.pileKind = kind;
    button.pileIndex = index;
    button.cardIndex = cardIndex;
    button.faceUp = card.face_up != 0;
    button.frame = frame;
    button.layer.shadowPath = [UIBezierPath bezierPathWithRoundedRect:button.bounds cornerRadius:7.0].CGPath;
    [button setImage:[self imageForCard:card] forState:UIControlStateNormal];
    button.imageView.contentMode = UIViewContentModeScaleToFill;
    button.accessibilityTraits = UIAccessibilityTraitButton;
    if (card.face_up) {
        button.accessibilityLabel = [NSString stringWithFormat:@"%@ of %@, %@",
                                     [self rankName:card.rank], [self suitName:card.suit],
                                     [self labelForPile:kind index:index]];
        button.accessibilityIdentifier = [NSString stringWithFormat:@"card.%@.%u",
                                          @[@"clubs", @"diamonds", @"hearts", @"spades"][card.suit % 4], card.rank];
    } else {
        button.accessibilityLabel = [NSString stringWithFormat:@"Face-down card in %@",
                                     [self labelForPile:kind index:index]];
        button.accessibilityIdentifier = @"card.back";
    }
    [self styleCardButton:button];
    if (!wasFaceUp && card.face_up && !UIAccessibilityIsReduceMotionEnabled()) {
        [UIView transitionWithView:button duration:0.20 options:UIViewAnimationOptionTransitionFlipFromRight |
                             UIViewAnimationOptionBeginFromCurrentState | UIViewAnimationOptionAllowUserInteraction
                         animations:^{} completion:nil];
    }
    return button;
}

- (void)styleCardButton:(OKCardButton *)button {
    BOOL selected = _hasSelection && button.pileKind == _selectedKind &&
                    button.pileIndex == _selectedIndex && button.cardIndex == _selectedCard;
    BOOL hintSource = _hasHint && button.pileKind == _hintSourceKind &&
                      button.pileIndex == _hintSourceIndex &&
                      (button.pileKind == LOC_STOCK || button.cardIndex == _hintSourceCard);
    BOOL hintDestination = _hasHint && button.pileKind == _hintDestinationKind &&
                           button.pileIndex == _hintDestinationIndex && button.faceUp;
    BOOL highlighted = hintSource || hintDestination;
    button.layer.borderWidth = selected ? 2.5 : (highlighted ? 2.0 : 0.5);
    button.layer.borderColor = selected ? [UIColor colorWithRed:1 green:0.79 blue:0.25 alpha:1].CGColor :
                               (highlighted ? [UIColor colorWithRed:0.4 green:0.94 blue:0.72 alpha:1].CGColor :
                                [UIColor colorWithWhite:0 alpha:0.32].CGColor);
    button.accessibilityValue = hintSource ? @"Hint source" :
        (hintDestination ? @"Hint destination" : (selected ? @"Selected" : nil));
    button.transform = selected ? CGAffineTransformMakeScale(1.035, 1.035) : CGAffineTransformIdentity;
    button.accessibilityHint = button.faceUp ? @"Tap to move automatically. Press and hold to choose a destination." : nil;
}

- (void)positionCards {
    if (!_hasGame) return;
    NSSet *allKeys = [NSSet setWithArray:_cardButtons.allKeys];
    NSMutableSet *usedKeys = [NSMutableSet setWithCapacity:52];
    OKPileButton *stockSlot = _slotButtons[[self slotKeyForKind:LOC_STOCK index:0]];
    UIImageSymbolConfiguration *recycleSymbol =
        [UIImageSymbolConfiguration configurationWithPointSize:17 weight:UIImageSymbolWeightMedium];
    if (_game.stock.count == 0 && _game.waste.count > 0) {
        [stockSlot setImage:[UIImage systemImageNamed:@"arrow.clockwise" withConfiguration:recycleSymbol]
                   forState:UIControlStateNormal];
        stockSlot.tintColor = [UIColor colorWithWhite:1 alpha:0.54];
        stockSlot.accessibilityLabel = @"Recycle waste into stock";
    } else {
        [stockSlot setImage:nil forState:UIControlStateNormal];
        stockSlot.accessibilityLabel = @"Draw from stock";
    }

    // Only the top stock card is visible; its button is still a regular card
    // back so the deck art and proportions stay consistent.
    if (_game.stock.count > 0) {
        NSInteger c = _game.stock.count - 1;
        Card card = _game.stock.cards[c];
        NSNumber *key = @([self keyForCard:card]);
        [usedKeys addObject:key];
        [self buttonForCard:card kind:LOC_STOCK index:0 cardIndex:c
                      frame:CGRectMake(_columnX[0] + _topInset, 0, _topCardWidth, _topCardHeight)];
    }

    // Draw-three waste shows up to the three exposed cards, with the newest on
    // top. Older exposed cards can be seen but only the newest is movable.
    NSInteger wasteStart = MAX(0, _game.waste.count - MIN(3, MAX(1, _game.waste_drawn)));
    CGFloat wasteFan = _topCardWidth * 0.22;
    for (NSInteger c = wasteStart; c < _game.waste.count; c++) {
        Card card = _game.waste.cards[c];
        NSNumber *key = @([self keyForCard:card]);
        [usedKeys addObject:key];
        CGFloat offset = (CGFloat)(c - wasteStart) * wasteFan;
        [self buttonForCard:card kind:LOC_WASTE index:0 cardIndex:c
                      frame:CGRectMake(_columnX[1] + _topInset + offset, 0, _topCardWidth, _topCardHeight)];
    }

    for (NSInteger f = 0; f < 4; f++) {
        const Pile *pile = &_game.foundation[f];
        if (pile->count == 0) continue;
        NSInteger c = pile->count - 1;
        Card card = pile->cards[c];
        NSNumber *key = @([self keyForCard:card]);
        [usedKeys addObject:key];
        [self buttonForCard:card kind:LOC_FOUNDATION index:f cardIndex:c
                      frame:CGRectMake(_columnX[f + 3] + _topInset, 0, _topCardWidth, _topCardHeight)];
    }

    for (NSInteger column = 0; column < 7; column++) {
        const Pile *pile = &_game.tableau[column];
        CGFloat y = _tableauY;
        for (NSInteger c = 0; c < pile->count; c++) {
            Card card = pile->cards[c];
            NSNumber *key = @([self keyForCard:card]);
            [usedKeys addObject:key];
            [self buttonForCard:card kind:LOC_TABLEAU index:column cardIndex:c
                          frame:CGRectMake(_columnX[column], y, _cardWidth, _cardHeight)];
            if (c + 1 < pile->count) y += card.face_up ? _faceUpSpacing : _faceDownSpacing;
        }
    }

    for (NSNumber *key in allKeys) {
        if (![usedKeys containsObject:key]) [_cardButtons[key] removeFromSuperview];
    }
    // Reestablish pile order after reusing card views: later cards sit above
    // the visible fan and remain the first accessible hit target.
    if (_game.stock.count > 0) {
        Card card = _game.stock.cards[_game.stock.count - 1];
        OKCardButton *button = _cardButtons[@([self keyForCard:card])];
        if (button) [self bringSubviewToFront:button];
    }
    for (NSInteger c = 0; c < _game.waste.count; c++) {
        if (c >= wasteStart) {
            Card card = _game.waste.cards[c];
            OKCardButton *button = _cardButtons[@([self keyForCard:card])];
            if (button) [self bringSubviewToFront:button];
        }
    }
    for (NSInteger f = 0; f < 4; f++) {
        if (_game.foundation[f].count == 0) continue;
        Card card = _game.foundation[f].cards[_game.foundation[f].count - 1];
        OKCardButton *button = _cardButtons[@([self keyForCard:card])];
        if (button) [self bringSubviewToFront:button];
    }
    for (NSInteger column = 0; column < 7; column++) {
        for (NSInteger c = 0; c < _game.tableau[column].count; c++) {
            Card card = _game.tableau[column].cards[c];
            OKCardButton *button = _cardButtons[@([self keyForCard:card])];
            if (button) [self bringSubviewToFront:button];
        }
    }

    for (OKCardButton *button in _cardButtons.allValues) [self styleCardButton:button];
    [self updateSlotStyles];
}

- (void)updateSlotStyles {
    for (NSString *key in _slotButtons) {
        OKPileButton *button = _slotButtons[key];
        PileKind kind = button.pileKind;
        NSInteger index = button.pileIndex;
        NSString *destinationKey = [self slotKeyForKind:kind index:index];
        BOOL selected = _hasSelection && [_legalDestinations containsObject:destinationKey];
        BOOL hinted = _hasHint && _hintDestinationKind == kind && _hintDestinationIndex == index;
        button.layer.borderWidth = selected || hinted ? 2.0 : 1.0;
        button.layer.borderColor = selected ? [UIColor colorWithRed:1 green:0.79 blue:0.25 alpha:0.95].CGColor :
                                   (hinted ? [UIColor colorWithRed:0.4 green:0.94 blue:0.72 alpha:0.95].CGColor :
                                    [UIColor colorWithWhite:1.0 alpha:0.19].CGColor);
        button.backgroundColor = selected ? [UIColor colorWithRed:0.82 green:0.64 blue:0.18 alpha:0.20] :
                                 (hinted ? [UIColor colorWithRed:0.20 green:0.78 blue:0.56 alpha:0.20] :
                                  [UIColor colorWithRed:0.025 green:0.23 blue:0.12 alpha:0.24]);
        button.accessibilityHint = selected ? @"Legal destination for the selected card." : nil;
        button.accessibilityValue = hinted ? @"Hint destination" : nil;
    }
}

- (void)selectKind:(PileKind)kind index:(NSInteger)index card:(NSInteger)card {
    _hasSelection = YES;
    _selectedKind = kind;
    _selectedIndex = index;
    _selectedCard = card;
    [_selectionFeedback selectionChanged];
    [_legalDestinations removeAllObjects];
    if (_hasGame) {
        for (NSInteger f = 0; f < 4; f++) {
            if (game_can_drop(&_game, kind, (int)index, (int)card, LOC_FOUNDATION, (int)f))
                [_legalDestinations addObject:[self slotKeyForKind:LOC_FOUNDATION index:f]];
        }
        for (NSInteger t = 0; t < 7; t++) {
            if (game_can_drop(&_game, kind, (int)index, (int)card, LOC_TABLEAU, (int)t))
                [_legalDestinations addObject:[self slotKeyForKind:LOC_TABLEAU index:t]];
        }
    }
    [self positionCards];
}

- (void)clearSelection {
    _hasSelection = NO;
    [_legalDestinations removeAllObjects];
    [self positionCards];
}

- (void)setLegalDestinations:(NSArray<NSString *> *)destinations {
    [_legalDestinations removeAllObjects];
    [_legalDestinations addObjectsFromArray:destinations ?: @[]];
    [self updateSlotStyles];
}

- (void)setHintSourceKind:(PileKind)sourceKind sourceIndex:(NSInteger)sourceIndex
               sourceCard:(NSInteger)sourceCard destinationKind:(PileKind)destinationKind
        destinationIndex:(NSInteger)destinationIndex {
    _hasHint = YES;
    _hintSourceKind = sourceKind;
    _hintSourceIndex = sourceIndex;
    _hintSourceCard = sourceCard;
    _hintDestinationKind = destinationKind;
    _hintDestinationIndex = destinationIndex;
    [self positionCards];
}

- (void)clearHint {
    _hasHint = NO;
    [self positionCards];
}

- (void)stockSlotPressed:(OKPileButton *)sender {
    (void)sender;
    [self.delegate boardViewDidTapStock:self];
}

- (void)slotPressed:(OKPileButton *)sender {
    [self.delegate boardView:self didTapDestinationKind:sender.pileKind index:sender.pileIndex];
}

- (void)cardPressed:(OKCardButton *)sender {
    [self.delegate boardView:self didTapCardKind:sender.pileKind index:sender.pileIndex card:sender.cardIndex];
}

- (void)cardLongPressed:(UILongPressGestureRecognizer *)gesture {
    if (gesture.state != UIGestureRecognizerStateBegan) return;
    OKCardButton *sender = (OKCardButton *)gesture.view;
    if (!sender.faceUp) return;
    [self.delegate boardView:self didLongPressCardKind:sender.pileKind
                       index:sender.pileIndex card:sender.cardIndex];
}

@end

@protocol OKFrameTarget <NSObject>
- (void)stepGame:(CADisplayLink *)displayLink;
@end

@interface OKFrameProxy : NSObject
@property (nonatomic, weak) id<OKFrameTarget> target;
- (void)onFrame:(CADisplayLink *)displayLink;
@end
@implementation OKFrameProxy
- (void)onFrame:(CADisplayLink *)displayLink { [self.target stepGame:displayLink]; }
@end

@interface SolitaireViewController () <OKBoardViewDelegate, OKFrameTarget>
- (void)clearHint;
- (void)clearSelection;
- (void)updateDrawModeControl;
- (void)restoreWinnablePosition;
@end

@implementation SolitaireViewController {
    OKSession *_session;
    OKBoardView *_boardView;
    UIImageView *_feltView;
    UILabel *_statusLabel;
    UIView *_statusDot;
    UILabel *_movesLabel;
    UILabel *_timeLabel;
    UIButton *_drawModeButton;
    UIButton *_undoButton;
    UIButton *_hintButton;
    UIButton *_newGameButton;
    CADisplayLink *_displayLink;
    CFTimeInterval _lastFrameTime;
    SolverStatus _lastSolverStatus;
    NSInteger _lastMoves;
    NSInteger _lastSeconds;
    BOOL _hasStatus;
    BOOL _captureMode;
    BOOL _sceneActive;
    BOOL _shownLossAlert;
    DrawMode _drawMode;
    PileKind _selectedKind;
    NSInteger _selectedIndex;
    NSInteger _selectedCard;
    BOOL _hasSelection;
    UIImpactFeedbackGenerator *_haptic;
    UISelectionFeedbackGenerator *_selectionHaptic;
    SolverMove _hintMove;
    BOOL _hasHintMove;
    CAGradientLayer *_feltShadeLayer;
    NSLayoutConstraint *_headerHeightConstraint;
    NSLayoutConstraint *_statusHeightConstraint;
    NSLayoutConstraint *_footerHeightConstraint;
}

- (instancetype)init {
    self = [super initWithNibName:nil bundle:nil];
    if (self) {
        NSArray<NSString *> *arguments = NSProcessInfo.processInfo.arguments;
        BOOL captureThree = [arguments containsObject:@"--capture-draw-three"];
        _captureMode = captureThree || [arguments containsObject:@"--capture-deal"];
        _drawMode = captureThree ? DRAW_THREE : DRAW_ONE;
        unsigned choice = _captureMode ? 0u : arc4random();
        _session = ok_session_create(_drawMode, choice);
        if (_session && !_captureMode) [self restoreSavedGameIfValid];
        const Game *game = _session ? ok_session_game(_session) : NULL;
        if (game) _drawMode = game->draw_mode;
        _sceneActive = YES;
        _lastSolverStatus = SOLVER_UNKNOWN;
        _haptic = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
        _selectionHaptic = [[UISelectionFeedbackGenerator alloc] init];
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(sceneWillPause:)
                                                     name:UIApplicationWillResignActiveNotification object:nil];
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(sceneDidResume:)
                                                     name:UIApplicationDidBecomeActiveNotification object:nil];
    }
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [_displayLink invalidate];
    [self saveCurrentGame];
    if (_session) ok_session_destroy(_session);
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor colorWithRed:0.025 green:0.28 blue:0.15 alpha:1.0];
    self.view.tintColor = [UIColor colorWithRed:1.0 green:0.79 blue:0.31 alpha:1.0];

    [self buildFeltBackground];
    UIView *header = [self buildHeader];
    UIView *status = [self buildStatusRow];
    _boardView = [[OKBoardView alloc] initWithFrame:CGRectZero];
    _boardView.translatesAutoresizingMaskIntoConstraints = NO;
    _boardView.delegate = self;
    [self.view addSubview:_boardView];
    UIView *footer = [self buildFooter];

    _headerHeightConstraint = [header.heightAnchor constraintEqualToConstant:43];
    _statusHeightConstraint = [status.heightAnchor constraintEqualToConstant:36];
    _footerHeightConstraint = [footer.heightAnchor constraintEqualToConstant:67];
    [NSLayoutConstraint activateConstraints:@[
        [header.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor],
        [header.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [header.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        _headerHeightConstraint,
        [status.topAnchor constraintEqualToAnchor:header.bottomAnchor constant:2],
        [status.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor constant:12],
        [status.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-12],
        _statusHeightConstraint,
        [_boardView.topAnchor constraintEqualToAnchor:status.bottomAnchor constant:8],
        [_boardView.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor constant:3],
        [_boardView.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-3],
        [_boardView.bottomAnchor constraintEqualToAnchor:footer.topAnchor constant:-8],
        [footer.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor constant:12],
        [footer.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-12],
        [footer.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor constant:-8],
        _footerHeightConstraint
    ]];

    const Game *game = _session ? ok_session_game(_session) : NULL;
    if (game) {
        [_boardView renderGame:game animated:NO];
        _lastSolverStatus = ok_session_status(_session);
        _hasStatus = YES;
        [self updateStatusForce:YES];
    } else {
        _statusLabel.text = @"Could not start a deal";
        _statusLabel.textColor = [UIColor colorWithRed:1 green:0.76 blue:0.68 alpha:1];
    }
    [self updateControlAvailability];
}

- (void)buildFeltBackground {
    NSString *path = [[[NSBundle mainBundle] resourcePath] stringByAppendingPathComponent:kCardsDirectory];
    UIImage *felt = [UIImage imageWithContentsOfFile:[path stringByAppendingPathComponent:@"felt.png"]];
    _feltView = [[UIImageView alloc] initWithImage:felt];
    _feltView.translatesAutoresizingMaskIntoConstraints = NO;
    _feltView.contentMode = UIViewContentModeScaleAspectFill;
    _feltView.clipsToBounds = YES;
    _feltView.backgroundColor = [UIColor colorWithRed:0.025 green:0.29 blue:0.16 alpha:1];
    [self.view addSubview:_feltView];
    [NSLayoutConstraint activateConstraints:@[
        [_feltView.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [_feltView.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
        [_feltView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [_feltView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor]
    ]];
    _feltShadeLayer = [CAGradientLayer layer];
    _feltShadeLayer.colors = @[(id)[UIColor colorWithRed:0.01 green:0.11 blue:0.06 alpha:0.37].CGColor,
                               (id)[UIColor clearColor].CGColor,
                               (id)[UIColor colorWithRed:0.005 green:0.08 blue:0.045 alpha:0.38].CGColor];
    _feltShadeLayer.locations = @[@0.0, @0.46, @1.0];
    _feltShadeLayer.startPoint = CGPointMake(0.5, 0.0);
    _feltShadeLayer.endPoint = CGPointMake(0.5, 1.0);
    [_feltView.layer addSublayer:_feltShadeLayer];
    // The felt image is optional at build time while the deck art is being
    // assembled. This keeps the surface rich and intentional either way.
    _feltShadeLayer.frame = _feltView.bounds;
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    _feltShadeLayer.frame = _feltView.bounds;
    if (!_headerHeightConstraint || !_statusHeightConstraint || !_footerHeightConstraint) return;
    BOOL landscape = CGRectGetWidth(self.view.bounds) > CGRectGetHeight(self.view.bounds);
    CGFloat headerHeight = landscape ? 30.0 : 43.0;
    CGFloat statusHeight = landscape ? 28.0 : 36.0;
    CGFloat footerHeight = landscape ? 56.0 : 67.0;
    if (_headerHeightConstraint.constant != headerHeight ||
        _statusHeightConstraint.constant != statusHeight ||
        _footerHeightConstraint.constant != footerHeight) {
        _headerHeightConstraint.constant = headerHeight;
        _statusHeightConstraint.constant = statusHeight;
        _footerHeightConstraint.constant = footerHeight;
        // Keep the full icon and title visible inside the compact landscape bar.
        for (UIButton *button in @[_undoButton, _hintButton, _newGameButton]) {
            UIButtonConfiguration *configuration = button.configuration;
            configuration.contentInsets = NSDirectionalEdgeInsetsMake(landscape ? 3 : 6, 4,
                                                                       landscape ? 3 : 6, 4);
            button.configuration = configuration;
        }

        [self.view setNeedsLayout];
    }
}

- (UIView *)buildHeader {
    UIView *header = [[UIView alloc] init];
    header.translatesAutoresizingMaskIntoConstraints = NO;
    header.backgroundColor = [UIColor colorWithRed:0.015 green:0.18 blue:0.095 alpha:0.56];
    [self.view addSubview:header];
    UILabel *title = [[UILabel alloc] init];
    title.translatesAutoresizingMaskIntoConstraints = NO;
    title.text = @"SOLITAIRE";
    title.font = [UIFont systemFontOfSize:15 weight:UIFontWeightSemibold];
    title.textColor = [UIColor colorWithRed:0.97 green:0.95 blue:0.87 alpha:1];
    title.textAlignment = NSTextAlignmentCenter;
    title.adjustsFontForContentSizeCategory = YES;
    [header addSubview:title];

    _drawModeButton = [UIButton buttonWithType:UIButtonTypeSystem];
    _drawModeButton.translatesAutoresizingMaskIntoConstraints = NO;
    _drawModeButton.layer.cornerRadius = 11;
    _drawModeButton.backgroundColor = [UIColor colorWithWhite:1 alpha:0.09];
    _drawModeButton.titleLabel.font = [UIFont systemFontOfSize:10 weight:UIFontWeightBold];
    [_drawModeButton setTitleColor:[UIColor colorWithRed:0.94 green:0.88 blue:0.66 alpha:1] forState:UIControlStateNormal];
    _drawModeButton.accessibilityLabel = @"Draw mode settings";
    _drawModeButton.accessibilityIdentifier = @"control.draw-mode";
    [_drawModeButton addTarget:self action:@selector(showSettings:) forControlEvents:UIControlEventTouchUpInside];
    [header addSubview:_drawModeButton];
    [self updateDrawModeControl];

    UIButton *settings = [UIButton buttonWithType:UIButtonTypeSystem];
    settings.translatesAutoresizingMaskIntoConstraints = NO;
    UIImageSymbolConfiguration *symbol = [UIImageSymbolConfiguration configurationWithPointSize:19 weight:UIImageSymbolWeightMedium];
    [settings setImage:[UIImage systemImageNamed:@"ellipsis.circle" withConfiguration:symbol] forState:UIControlStateNormal];
    settings.tintColor = [UIColor colorWithRed:0.95 green:0.91 blue:0.78 alpha:1];
    settings.accessibilityLabel = @"Settings";
    settings.accessibilityIdentifier = @"control.settings";
    [settings addTarget:self action:@selector(showSettings:) forControlEvents:UIControlEventTouchUpInside];
    [header addSubview:settings];
    [NSLayoutConstraint activateConstraints:@[
        [_drawModeButton.leadingAnchor constraintEqualToAnchor:header.safeAreaLayoutGuide.leadingAnchor constant:11],
        [_drawModeButton.centerYAnchor constraintEqualToAnchor:header.centerYAnchor],
        [_drawModeButton.widthAnchor constraintEqualToConstant:70],
        [_drawModeButton.heightAnchor constraintEqualToConstant:30],
        [title.centerXAnchor constraintEqualToAnchor:header.centerXAnchor],
        [title.centerYAnchor constraintEqualToAnchor:header.centerYAnchor],
        [settings.trailingAnchor constraintEqualToAnchor:header.safeAreaLayoutGuide.trailingAnchor constant:-5],
        [settings.centerYAnchor constraintEqualToAnchor:header.centerYAnchor],
        [settings.widthAnchor constraintEqualToConstant:44],
        [settings.heightAnchor constraintEqualToConstant:44]
    ]];
    UIView *rule = [[UIView alloc] init];
    rule.translatesAutoresizingMaskIntoConstraints = NO;
    rule.backgroundColor = [UIColor colorWithWhite:1 alpha:0.10];
    [header addSubview:rule];
    [NSLayoutConstraint activateConstraints:@[
        [rule.leadingAnchor constraintEqualToAnchor:header.leadingAnchor],
        [rule.trailingAnchor constraintEqualToAnchor:header.trailingAnchor],
        [rule.bottomAnchor constraintEqualToAnchor:header.bottomAnchor],
        [rule.heightAnchor constraintEqualToConstant:1.0 / UIScreen.mainScreen.scale]
    ]];
    return header;
}

- (UIView *)buildStatusRow {
    UIView *row = [[UIView alloc] init];
    row.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:row];
    UIView *chip = [[UIView alloc] init];
    chip.translatesAutoresizingMaskIntoConstraints = NO;
    chip.backgroundColor = [UIColor colorWithWhite:0 alpha:0.17];
    chip.layer.cornerRadius = 13;
    [row addSubview:chip];
    _statusDot = [[UIView alloc] init];
    _statusDot.translatesAutoresizingMaskIntoConstraints = NO;
    _statusDot.layer.cornerRadius = 4;
    _statusDot.backgroundColor = [UIColor colorWithRed:0.94 green:0.82 blue:0.48 alpha:1];
    [chip addSubview:_statusDot];
    _statusLabel = [[UILabel alloc] init];
    _statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    _statusLabel.font = [UIFont systemFontOfSize:13 weight:UIFontWeightSemibold];
    _statusLabel.textColor = [UIColor colorWithRed:0.93 green:0.90 blue:0.77 alpha:1];
    _statusLabel.adjustsFontForContentSizeCategory = YES;
    _statusLabel.text = @"Checking deal";
    [chip addSubview:_statusLabel];

    _movesLabel = [[UILabel alloc] init];
    _movesLabel.translatesAutoresizingMaskIntoConstraints = NO;
    _movesLabel.font = [UIFont monospacedDigitSystemFontOfSize:13 weight:UIFontWeightMedium];
    _movesLabel.textColor = [UIColor colorWithWhite:1 alpha:0.88];
    _movesLabel.textAlignment = NSTextAlignmentRight;
    _movesLabel.text = @"0 moves";
    [row addSubview:_movesLabel];
    _timeLabel = [[UILabel alloc] init];
    _timeLabel.translatesAutoresizingMaskIntoConstraints = NO;
    _timeLabel.font = [UIFont monospacedDigitSystemFontOfSize:13 weight:UIFontWeightRegular];
    _timeLabel.textColor = [UIColor colorWithWhite:1 alpha:0.70];
    _timeLabel.textAlignment = NSTextAlignmentRight;
    _timeLabel.text = @"00:00";
    [row addSubview:_timeLabel];

    [NSLayoutConstraint activateConstraints:@[
        [chip.leadingAnchor constraintEqualToAnchor:row.leadingAnchor],
        [chip.centerYAnchor constraintEqualToAnchor:row.centerYAnchor],
        [chip.heightAnchor constraintEqualToConstant:28],
        [_statusDot.leadingAnchor constraintEqualToAnchor:chip.leadingAnchor constant:10],
        [_statusDot.centerYAnchor constraintEqualToAnchor:chip.centerYAnchor],
        [_statusDot.widthAnchor constraintEqualToConstant:8],
        [_statusDot.heightAnchor constraintEqualToConstant:8],
        [_statusLabel.leadingAnchor constraintEqualToAnchor:_statusDot.trailingAnchor constant:7],
        [_statusLabel.trailingAnchor constraintEqualToAnchor:chip.trailingAnchor constant:-10],
        [_statusLabel.centerYAnchor constraintEqualToAnchor:chip.centerYAnchor],
        [_movesLabel.leadingAnchor constraintGreaterThanOrEqualToAnchor:chip.trailingAnchor constant:10],
        [_movesLabel.trailingAnchor constraintEqualToAnchor:_timeLabel.leadingAnchor constant:-13],
        [_movesLabel.centerYAnchor constraintEqualToAnchor:row.centerYAnchor],
        [_timeLabel.trailingAnchor constraintEqualToAnchor:row.trailingAnchor],
        [_timeLabel.centerYAnchor constraintEqualToAnchor:row.centerYAnchor]
    ]];
    return row;
}

- (UIButton *)actionButtonWithTitle:(NSString *)title symbol:(NSString *)symbol
                      identifier:(NSString *)identifier primary:(BOOL)primary {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    button.translatesAutoresizingMaskIntoConstraints = NO;
    UIButtonConfiguration *configuration = [UIButtonConfiguration plainButtonConfiguration];
    configuration.title = title;
    configuration.image = [UIImage systemImageNamed:symbol withConfiguration:
                             [UIImageSymbolConfiguration configurationWithPointSize:17 weight:UIImageSymbolWeightMedium]];
    configuration.imagePlacement = NSDirectionalRectEdgeTop;
    configuration.imagePadding = 4;
    configuration.contentInsets = NSDirectionalEdgeInsetsMake(6, 4, 6, 4);
    configuration.baseForegroundColor = primary ? [UIColor colorWithRed:0.15 green:0.20 blue:0.12 alpha:1] :
                                                [UIColor colorWithRed:0.95 green:0.93 blue:0.85 alpha:1];
    UIColor *ink = configuration.baseForegroundColor;
    configuration.image = [configuration.image imageWithTintColor:ink renderingMode:UIImageRenderingModeAlwaysOriginal];
    configuration.titleTextAttributesTransformer = ^NSDictionary<NSAttributedStringKey,id> *(NSDictionary<NSAttributedStringKey,id> *attributes) {
        NSMutableDictionary *result = [attributes mutableCopy];
        result[NSFontAttributeName] = [UIFont systemFontOfSize:11 weight:UIFontWeightSemibold];
        result[NSForegroundColorAttributeName] = ink;
        return result;
    };
    button.configuration = configuration;
    button.accessibilityIdentifier = identifier;
    button.layer.cornerRadius = 13;
    button.backgroundColor = primary ? [UIColor colorWithRed:0.91 green:0.76 blue:0.34 alpha:1] :
                                       [UIColor colorWithWhite:1 alpha:0.10];
    button.layer.borderWidth = primary ? 0 : 1;
    button.layer.borderColor = [UIColor colorWithWhite:1 alpha:0.13].CGColor;
    button.accessibilityTraits = UIAccessibilityTraitButton;
    return button;
}

- (UIView *)buildFooter {
    UIView *footer = [[UIView alloc] init];
    footer.translatesAutoresizingMaskIntoConstraints = NO;
    footer.backgroundColor = [UIColor colorWithRed:0.015 green:0.13 blue:0.07 alpha:0.76];
    footer.layer.cornerRadius = 16;
    footer.layer.borderWidth = 1;
    footer.layer.borderColor = [UIColor colorWithWhite:1 alpha:0.14].CGColor;
    footer.layer.shadowColor = UIColor.blackColor.CGColor;
    footer.layer.shadowOpacity = 0.22;
    footer.layer.shadowRadius = 9;
    footer.layer.shadowOffset = CGSizeMake(0, 4);
    [self.view addSubview:footer];

    UIStackView *stack = [[UIStackView alloc] init];
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    stack.axis = UILayoutConstraintAxisHorizontal;
    stack.distribution = UIStackViewDistributionFillEqually;
    stack.alignment = UIStackViewAlignmentFill;
    stack.spacing = 8;
    [footer addSubview:stack];
    _undoButton = [self actionButtonWithTitle:@"Undo" symbol:@"arrow.uturn.backward" identifier:@"control.undo" primary:NO];
    _hintButton = [self actionButtonWithTitle:@"Hint" symbol:@"lightbulb" identifier:@"control.hint" primary:NO];
    _newGameButton = [self actionButtonWithTitle:@"New Game" symbol:@"plus" identifier:@"control.new-game" primary:YES];
    [_undoButton addTarget:self action:@selector(undo:) forControlEvents:UIControlEventTouchUpInside];
    [_hintButton addTarget:self action:@selector(hint:) forControlEvents:UIControlEventTouchUpInside];
    [_newGameButton addTarget:self action:@selector(newGame:) forControlEvents:UIControlEventTouchUpInside];
    [stack addArrangedSubview:_undoButton];
    [stack addArrangedSubview:_hintButton];
    [stack addArrangedSubview:_newGameButton];
    [NSLayoutConstraint activateConstraints:@[
        [stack.leadingAnchor constraintEqualToAnchor:footer.leadingAnchor constant:8],
        [stack.trailingAnchor constraintEqualToAnchor:footer.trailingAnchor constant:-8],
        [stack.topAnchor constraintEqualToAnchor:footer.topAnchor constant:6],
        [stack.bottomAnchor constraintEqualToAnchor:footer.bottomAnchor constant:-6]
    ]];
    return footer;
}

- (UIStatusBarStyle)preferredStatusBarStyle { return UIStatusBarStyleLightContent; }

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    [self startDisplayLinkIfNeeded];
}

- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    [self stopDisplayLink];
    [self saveCurrentGame];
}

- (void)startDisplayLinkIfNeeded {
    if (!_sceneActive || _displayLink) return;
    _lastFrameTime = 0;
    OKFrameProxy *proxy = [[OKFrameProxy alloc] init];
    proxy.target = self;
    _displayLink = [CADisplayLink displayLinkWithTarget:proxy selector:@selector(onFrame:)];
    _displayLink.preferredFramesPerSecond = 30;
    [_displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}

- (void)stopDisplayLink {
    [_displayLink invalidate];
    _displayLink = nil;
    _lastFrameTime = 0;
}

- (void)sceneWillPause:(NSNotification *)note {
    (void)note;
    _sceneActive = NO;
    [self stopDisplayLink];
    [self saveCurrentGame];
}

- (void)sceneDidResume:(NSNotification *)note {
    (void)note;
    _sceneActive = YES;
    [self startDisplayLinkIfNeeded];
}

- (void)stepGame:(CADisplayLink *)displayLink {
    if (!_session || !_sceneActive) return;
    CFTimeInterval now = displayLink.timestamp;
    double elapsed = _lastFrameTime > 0 ? (double)(now - _lastFrameTime) : 0.0;
    _lastFrameTime = now;
    if (self.presentedViewController) return;
    if (elapsed > 0.12) elapsed = 0.12;
    if (elapsed < 0.0) elapsed = 0.0;
    ok_session_step(_session, elapsed);
    [self refreshStatusIfNeeded];
}

- (NSString *)statusTextForGame:(const Game *)game status:(SolverStatus)status {
    if (game && game->phase == PHASE_WON) return @"Solved";
    switch (status) {
        case SOLVER_CHECKING: {
            size_t states = ok_session_search_states(_session);
            return states >= 1000 ? [NSString stringWithFormat:@"Checking · %luk", (unsigned long)(states / 1000)]
                                  : [NSString stringWithFormat:@"Checking · %lu", (unsigned long)states];
        }
        case SOLVER_WINNABLE: return @"Winnable";
        case SOLVER_UNWINNABLE: return @"Unwinnable";
        case SOLVER_UNKNOWN: return @"Not determined";
    }
    return @"Not determined";
}

- (UIColor *)statusColorForStatus:(SolverStatus)status game:(const Game *)game {
    if (game && game->phase == PHASE_WON) return [UIColor colorWithRed:0.70 green:0.94 blue:0.68 alpha:1];
    switch (status) {
        case SOLVER_WINNABLE: return [UIColor colorWithRed:0.70 green:0.91 blue:0.69 alpha:1];
        case SOLVER_UNWINNABLE: return [UIColor colorWithRed:1.0 green:0.53 blue:0.43 alpha:1];
        case SOLVER_CHECKING: return [UIColor colorWithRed:0.95 green:0.82 blue:0.47 alpha:1];
        case SOLVER_UNKNOWN: return [UIColor colorWithRed:0.82 green:0.84 blue:0.77 alpha:1];
    }
}

- (void)updateStatusForce:(BOOL)force {
    if (!_session || !_statusLabel) return;
    const Game *game = ok_session_game(_session);
    if (!game) return;
    SolverStatus status = ok_session_status(_session);
    NSInteger seconds = MAX(0, game->timer_frames / 60);
    if (force || status != _lastSolverStatus || game->moves != _lastMoves || seconds != _lastSeconds) {
        _statusLabel.text = [self statusTextForGame:game status:status];
        UIColor *color = [self statusColorForStatus:status game:game];
        _statusLabel.textColor = color;
        _statusDot.backgroundColor = color;
        _movesLabel.text = [NSString stringWithFormat:@"%d %@", game->moves, game->moves == 1 ? @"move" : @"moves"];
        _timeLabel.text = [NSString stringWithFormat:@"%02ld:%02ld", (long)(seconds / 60), (long)(seconds % 60)];
        [self updateControlAvailability];
        BOOL justLost = _hasStatus && status == SOLVER_UNWINNABLE && _lastSolverStatus != SOLVER_UNWINNABLE;
        if (status != SOLVER_UNWINNABLE) _shownLossAlert = NO;
        _lastSolverStatus = status;
        _lastMoves = game->moves;
        _lastSeconds = seconds;
        _hasStatus = YES;
        if (justLost) [self showUnwinnableAlert];
    }
}

- (void)refreshStatusIfNeeded {
    if (!_session) return;
    SolverStatus current = ok_session_status(_session);
    const Game *game = ok_session_game(_session);
    NSInteger seconds = game ? MAX(0, game->timer_frames / 60) : 0;
    if (!_hasStatus || current != _lastSolverStatus || (game && game->moves != _lastMoves) || seconds != _lastSeconds)
        [self updateStatusForce:NO];
}

- (void)updateControlAvailability {
    if (!_session) return;
    _undoButton.enabled = ok_session_can_undo(_session);
    _undoButton.alpha = _undoButton.enabled ? 1.0 : 0.48;
    _hintButton.enabled = YES;
    _newGameButton.enabled = YES;
}

- (void)saveCurrentGame {
    if (!_session || _captureMode) return;
    const Game *game = ok_session_game(_session);
    if (!game) return;
    OKSavedGameBlob blob = {};
    blob.magic = kSaveMagic;
    blob.version = kSaveVersion;
    blob.gameBytes = (uint32_t)sizeof(Game);
    memcpy(&blob.game, game, sizeof(Game));
    NSData *data = [NSData dataWithBytes:&blob length:sizeof(blob)];
    [NSUserDefaults.standardUserDefaults setObject:data forKey:kSavedGameKey];
}

- (void)restoreSavedGameIfValid {
    NSData *data = [NSUserDefaults.standardUserDefaults dataForKey:kSavedGameKey];
    if (![data isKindOfClass:NSData.class] || data.length != sizeof(OKSavedGameBlob)) return;
    OKSavedGameBlob blob;
    memcpy(&blob, data.bytes, sizeof(blob));
    if (blob.magic != kSaveMagic || blob.version != kSaveVersion || blob.gameBytes != sizeof(Game)) return;
    if (!ok_session_load(_session, &blob.game)) {
        [NSUserDefaults.standardUserDefaults removeObjectForKey:kSavedGameKey];
        return;
    }
    _drawMode = blob.game.draw_mode;
}

- (void)boardViewDidTapStock:(OKBoardView *)board {
    (void)board;
    if (!_session) return;
    [self clearSelection];
    BOOL isHintedDraw = _hasHintMove && _hintMove.type == SOLVER_MOVE_DRAW;
    if (ok_session_draw(_session)) {
        [_haptic impactOccurred];
        if (isHintedDraw) [self clearHint];
        [self afterGameAction];
    }
}

- (void)boardView:(OKBoardView *)board didTapCardKind:(PileKind)kind index:(NSInteger)index card:(NSInteger)cardIndex {
    (void)board;
    if (!_session) return;
    if (kind == LOC_STOCK) {
        [self boardViewDidTapStock:_boardView];
        return;
    }
    const Game *game = ok_session_game(_session);
    if (!game || !game_can_grab(game, kind, (int)index, (int)cardIndex)) return;
    BOOL isHintedSource = _hasHintMove && _hintMove.type == SOLVER_MOVE_CARD &&
                         kind == (PileKind)_hintMove.from_kind && index == _hintMove.from_index &&
                         cardIndex == _hintMove.card_index;
    if (isHintedSource && ok_session_move(_session, (PileKind)_hintMove.from_kind,
                                          _hintMove.from_index, _hintMove.card_index,
                                          (PileKind)_hintMove.to_kind, _hintMove.to_index)) {
        [self clearSelection];
        [self clearHint];
        [_haptic impactOccurred];
        [self afterGameAction];
        return;
    }
    if (_hasSelection) {
        BOOL same = kind == _selectedKind && index == _selectedIndex && cardIndex == _selectedCard;
        if (same) { [self clearSelection]; return; }
        BOOL selectedIsHintSource = _hasHintMove && _hintMove.type == SOLVER_MOVE_CARD &&
                                    _selectedKind == (PileKind)_hintMove.from_kind &&
                                    _selectedIndex == _hintMove.from_index &&
                                    _selectedCard == _hintMove.card_index;
        if (selectedIsHintSource && kind == (PileKind)_hintMove.to_kind &&
            index == _hintMove.to_index &&
            ok_session_move(_session, (PileKind)_hintMove.from_kind, _hintMove.from_index,
                            _hintMove.card_index, (PileKind)_hintMove.to_kind, _hintMove.to_index)) {
            [self clearSelection];
            [self clearHint];
            [_haptic impactOccurred];
            [self afterGameAction];
            return;
        }
        if ([self tryMoveToKind:kind index:index]) return;
        // A failed destination tap becomes a new source selection. Do not
        // surprise the player by auto-moving the card they meant to inspect.
        [self clearSelection];
        if (!isHintedSource) [self clearHint];
        _selectedKind = kind;
        _selectedIndex = index;
        _selectedCard = cardIndex;
        _hasSelection = YES;
        [_boardView selectKind:kind index:index card:cardIndex];
        return;
    }
    if (ok_session_auto_move(_session, kind, (int)index, (int)cardIndex)) {
        [self clearSelection];
        [_haptic impactOccurred];
        [self afterGameAction];
        return;
    }
    _selectedKind = kind;
    _selectedIndex = index;
    _selectedCard = cardIndex;
    _hasSelection = YES;
    [_boardView selectKind:kind index:index card:cardIndex];
}

- (void)boardView:(OKBoardView *)board didLongPressCardKind:(PileKind)kind index:(NSInteger)index card:(NSInteger)cardIndex {
    (void)board;
    if (!_session) return;
    const Game *game = ok_session_game(_session);
    if (!game || !game_can_grab(game, kind, (int)index, (int)cardIndex)) return;
    BOOL isHintSource = _hasHintMove && _hintMove.type == SOLVER_MOVE_CARD &&
                        kind == (PileKind)_hintMove.from_kind && index == _hintMove.from_index &&
                        cardIndex == _hintMove.card_index;
    [self clearSelection];
    if (!isHintSource) [self clearHint];
    _selectedKind = kind;
    _selectedIndex = index;
    _selectedCard = cardIndex;
    _hasSelection = YES;
    [_boardView selectKind:kind index:index card:cardIndex];
    [_selectionHaptic selectionChanged];
}

- (void)boardView:(OKBoardView *)board didTapDestinationKind:(PileKind)kind index:(NSInteger)index {
    (void)board;
    if (!_hasSelection) return;
    [self tryMoveToKind:kind index:index];
}

- (BOOL)tryMoveToKind:(PileKind)kind index:(NSInteger)index {
    if (!_hasSelection || !_session) return NO;
    BOOL selectedIsHintSource = _hasHintMove && _hintMove.type == SOLVER_MOVE_CARD &&
                                _selectedKind == (PileKind)_hintMove.from_kind &&
                                _selectedIndex == _hintMove.from_index &&
                                _selectedCard == _hintMove.card_index;
    BOOL exactHintDestination = selectedIsHintSource && kind == (PileKind)_hintMove.to_kind &&
                                index == _hintMove.to_index;
    BOOL moved = exactHintDestination
        ? ok_session_move(_session, (PileKind)_hintMove.from_kind, _hintMove.from_index,
                          _hintMove.card_index, (PileKind)_hintMove.to_kind, _hintMove.to_index)
        : ok_session_move(_session, _selectedKind, (int)_selectedIndex, (int)_selectedCard,
                          kind, (int)index);
    if (moved) {
        [self clearSelection];
        [_haptic impactOccurred];
        [self afterGameAction];
        return YES;
    }
    return NO;
}

- (void)clearSelection {
    _hasSelection = NO;
    [_boardView clearSelection];
}

- (void)clearHint {
    _hasHintMove = NO;
    [_boardView clearHint];
}

- (void)afterGameAction {
    if (!_session) return;
    [self clearHint];
    [_boardView renderGame:ok_session_game(_session) animated:YES];
    [self saveCurrentGame];
    [self updateStatusForce:YES];
}

- (void)undo:(id)sender {
    (void)sender;
    if (_session && ok_session_undo(_session)) {
        [self clearSelection];
        [_boardView clearHint];
        [_selectionHaptic selectionChanged];
        [self afterGameAction];
    }
}

- (void)hint:(id)sender {
    (void)sender;
    if (!_session) return;
    [self clearSelection];
    [self clearHint];
    SolverMove move;
    if (!ok_session_hint(_session, &move)) {
        _statusLabel.text = @"No hint available yet";
        _statusLabel.textColor = [UIColor colorWithRed:0.95 green:0.82 blue:0.47 alpha:1];
        return;
    }
    _hintMove = move;
    _hasHintMove = YES;
    [_boardView clearHint];
    if (move.type == SOLVER_MOVE_DRAW) {
        [_boardView clearSelection];
        [_boardView setHintSourceKind:LOC_STOCK sourceIndex:0 sourceCard:0
                    destinationKind:LOC_STOCK destinationIndex:0];
    } else {
        [_boardView setHintSourceKind:(PileKind)move.from_kind sourceIndex:move.from_index
                           sourceCard:move.card_index destinationKind:(PileKind)move.to_kind
                    destinationIndex:move.to_index];
    }
}

- (void)newGame:(id)sender {
    (void)sender;
    const Game *game = _session ? ok_session_game(_session) : NULL;
    BOOL hasProgress = game && (game->moves > 0 || game->stock_passes > 0 || game->waste.count > 0);
    if (!hasProgress) { [self startNewGameWithMode:_drawMode]; return; }
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Start a new game?"
                                                                   message:@"Your current deal will be replaced."
                                                            preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"Keep Playing" style:UIAlertActionStyleCancel handler:nil]];
    [alert addAction:[UIAlertAction actionWithTitle:@"New Game" style:UIAlertActionStyleDestructive
                                            handler:^(__unused UIAlertAction *action) { [self startNewGameWithMode:self->_drawMode]; }]];
    [self presentViewController:alert animated:YES completion:nil];
}

- (void)startNewGameWithMode:(DrawMode)mode {
    if (!_session || !ok_session_new(_session, mode, arc4random())) return;
    _drawMode = mode;
    [self updateDrawModeControl];
    _shownLossAlert = NO;
    _hasStatus = NO;
    [self clearSelection];
    [self clearHint];
    [_boardView renderGame:ok_session_game(_session) animated:YES];
    [self saveCurrentGame];
    [self updateStatusForce:YES];
}

- (void)showSettings:(id)sender {
    UIButton *source = (UIButton *)sender;
    UIAlertController *sheet = [UIAlertController alertControllerWithTitle:@"Game settings"
                                                                   message:(_session && ok_session_status(_session) == SOLVER_UNKNOWN)
                ? [NSString stringWithFormat:@"No conclusion after %@ positions. %@",
                    [NSNumberFormatter localizedStringFromNumber:@(ok_session_search_states(_session)) numberStyle:NSNumberFormatterDecimalStyle],
                    ok_session_can_check_deeper(_session) ? @"A deeper check may help."
                        : @"Export this game so the exact deal can be investigated."]
                : @"Choose how cards are drawn from the stock."
                                                            preferredStyle:UIAlertControllerStyleActionSheet];
    NSString *one = _drawMode == DRAW_ONE ? @"Draw one card · Current" : @"Draw one card";
    NSString *three = _drawMode == DRAW_THREE ? @"Draw three cards · Current" : @"Draw three cards";
    __weak SolitaireViewController *weakSelf = self;
    [sheet addAction:[UIAlertAction actionWithTitle:one style:UIAlertActionStyleDefault
                                              handler:^(__unused UIAlertAction *action) { [weakSelf requestModeChange:DRAW_ONE]; }]];
    [sheet addAction:[UIAlertAction actionWithTitle:three style:UIAlertActionStyleDefault
                                              handler:^(__unused UIAlertAction *action) { [weakSelf requestModeChange:DRAW_THREE]; }]];
    if (ok_session_can_check_deeper(_session)) {
        [sheet addAction:[UIAlertAction actionWithTitle:@"Check position" style:UIAlertActionStyleDefault
            handler:^(__unused UIAlertAction *action) { [weakSelf checkPosition]; }]];
    }
    if (_session && ok_session_can_restore(_session)) {
        [sheet addAction:[UIAlertAction actionWithTitle:@"Restore last winnable position"
                                                  style:UIAlertActionStyleDefault
                                                handler:^(__unused UIAlertAction *action) { [weakSelf restoreWinnablePosition]; }]];
    }
    [sheet addAction:[UIAlertAction actionWithTitle:@"Export game" style:UIAlertActionStyleDefault
        handler:^(__unused UIAlertAction *action) { [weakSelf exportPosition]; }]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    UIPopoverPresentationController *popover = sheet.popoverPresentationController;
    popover.sourceView = source ?: self.view;
    popover.sourceRect = source ? source.bounds : CGRectMake(CGRectGetMidX(self.view.bounds), 30, 1, 1);
    [self presentViewController:sheet animated:YES completion:nil];
}

- (void)exportPosition {
    if (!_session) return;
    uint8_t bytes[OK_POSITION_BYTES];
    if (!position_encode(ok_session_game(_session), bytes)) return;
    NSString *name = [NSString stringWithFormat:@"Still-Solvable-%@.solitaire", NSUUID.UUID.UUIDString];
    NSURL *url = [[NSURL fileURLWithPath:NSTemporaryDirectory() isDirectory:YES] URLByAppendingPathComponent:name];
    NSError *error = nil;
    if (![[NSData dataWithBytes:bytes length:sizeof(bytes)] writeToURL:url options:NSDataWritingAtomic error:&error]) {
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Couldn't export this game"
            message:@"The file couldn't be created. Please try again."
            preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleCancel handler:nil]];
        [self dismissViewControllerAnimated:YES completion:^{ [self presentViewController:alert animated:YES completion:nil]; }];
        return;
    }
    UIActivityViewController *activity = [[UIActivityViewController alloc] initWithActivityItems:@[url] applicationActivities:nil];
    activity.popoverPresentationController.sourceView = self.view;
    activity.popoverPresentationController.sourceRect = CGRectMake(CGRectGetMidX(self.view.bounds), CGRectGetMidY(self.view.bounds), 1, 1);
    activity.completionWithItemsHandler = ^(__unused UIActivityType type, __unused BOOL completed,
        __unused NSArray *items, __unused NSError *activityError) {
        [NSFileManager.defaultManager removeItemAtURL:url error:nil];
    };
    if (self.presentedViewController) {
        [self dismissViewControllerAnimated:YES completion:^{ [self presentViewController:activity animated:YES completion:nil]; }];
    } else {
        [self presentViewController:activity animated:YES completion:nil];
    }
}

- (void)checkPosition {
    if (!_session) return;
    [self clearHint];
    ok_session_check(_session);
    [self updateStatusForce:YES];
}

- (void)updateDrawModeControl {
    if (!_drawModeButton) return;
    [_drawModeButton setTitle:_drawMode == DRAW_THREE ? @"DRAW 3" : @"DRAW 1"
                      forState:UIControlStateNormal];
}

- (void)restoreWinnablePosition {
    if (!_session || !ok_session_restore(_session)) return;
    _shownLossAlert = NO;
    [self clearSelection];
    [self clearHint];
    [_boardView renderGame:ok_session_game(_session) animated:YES];
    [self saveCurrentGame];
    [self updateStatusForce:YES];
}

- (void)requestModeChange:(DrawMode)mode {
    if (mode == _drawMode) return;
    const Game *game = _session ? ok_session_game(_session) : NULL;
    BOOL hasProgress = game && (game->moves > 0 || game->stock_passes > 0 || game->waste.count > 0);
    if (!hasProgress) { [self startNewGameWithMode:mode]; return; }
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Change draw mode?"
                                                                   message:@"A new deal is needed to change this setting."
                                                            preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Start New Deal" style:UIAlertActionStyleDefault
                                            handler:^(__unused UIAlertAction *action) { [self startNewGameWithMode:mode]; }]];
    [self presentViewController:alert animated:YES completion:nil];
}

- (void)showUnwinnableAlert {
    if (_shownLossAlert || !self.view.window) return;
    _shownLossAlert = YES;
    BOOL canUndo = _session && ok_session_can_undo(_session);
    BOOL canRestore = _session && ok_session_can_restore(_session);
    NSString *message = (canUndo || canRestore)
        ? @"The game has checked every legal continuation from this position. Undo or restore a proven winning position to continue."
        : @"The game has checked every legal continuation from this position. Start a new deal to continue.";
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"This deal is unwinnable"
                                                                   message:message
                                                            preferredStyle:UIAlertControllerStyleAlert];
    if (canUndo) {
        [alert addAction:[UIAlertAction actionWithTitle:@"Undo Last Move" style:UIAlertActionStyleDefault
                                                handler:^(__unused UIAlertAction *action) { [self undo:nil]; }]];
    }
    if (canRestore) {
        [alert addAction:[UIAlertAction actionWithTitle:@"Restore Winning Position" style:UIAlertActionStyleDefault
                                                handler:^(__unused UIAlertAction *action) { [self restoreWinnablePosition]; }]];
    }
    [alert addAction:[UIAlertAction actionWithTitle:@"Continue" style:UIAlertActionStyleCancel handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

@end
