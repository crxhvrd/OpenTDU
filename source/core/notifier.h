#pragma once

class Notifier;

class Notifiable {
public:
    Notifiable();
    Notifiable( const Notifiable &other );
    virtual ~Notifiable();

    Notifiable &operator=( const Notifiable &other );

    void subscribe( Notifier *pNotifier );
    void unsubscribe( Notifier *pNotifier );
    void removeAll();
    void notify( uint32_t param = 0u );

    virtual bool isValid() { return false; }

protected:
    std::vector<Notifier *> notifiers;

private:
    void copyNotifiers( const Notifiable &other );
};

class Notifier {
public:
    inline void increaseReferenceCount() { referenceCount++; }
    inline void decreaseReferenceCount() { referenceCount--; }
    void addNotifiable( Notifiable *pNotifiable );

public:
    Notifier();
    virtual ~Notifier();

    virtual void onUpdate( uint32_t param );
    void removeNotifiable( Notifiable *pNotifiable );

protected:
    int32_t referenceCount;
    std::vector<Notifiable *> notifiableInstances;
};
